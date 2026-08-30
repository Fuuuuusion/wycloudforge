'use strict';

const http = require('node:http');
const { randomUUID } = require('node:crypto');
const path = require('node:path');
const { Worker } = require('node:worker_threads');
const sdk = require('@sansenjian/qq-music-api/sdk');
const services = require('@sansenjian/qq-music-api/services');
const contract = require('./contract');

const HOST = '127.0.0.1';
const PORT = Number(process.env.PORT || 3200);
const SERVICE = 'wycloudforge-qq-wrapper';
const ATTEMPT_TTL_MS = 5 * 60 * 1000;
const UPSTREAM_TIMEOUT_MS = 15000;
const attempts = new Map();

function send(response, status, payload) {
  const body = Buffer.from(JSON.stringify(payload));
  response.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': body.length,
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff',
  });
  response.end(body);
}

function success(response, data = {}) {
  send(response, 200, { ok: true, data, error: null });
}

function failure(response, status, code, message) {
  send(response, status, { ok: false, data: null, error: { code, message } });
}

async function readJson(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > 1024 * 1024) throw new Error('请求体过大');
    chunks.push(chunk);
  }
  if (!chunks.length) return {};
  return JSON.parse(Buffer.concat(chunks).toString('utf8'));
}

function cookieValue(cookie, names) {
  const values = new Map(String(cookie || '').split(';').map((item) => {
    const index = item.indexOf('=');
    return index > 0 ? [item.slice(0, index).trim(), item.slice(index + 1).trim()] : ['', ''];
  }));
  for (const name of names) {
    const value = values.get(name);
    if (value) return value.replace(/^o/, '');
  }
  return '';
}

function accountUserId(credential, fallback = '') {
  return (cookieValue(credential, ['qqmusic_uin', 'uin', 'wxuin']) || String(fallback || ''))
    .replace(/^o/, '');
}

async function fetchJson(url, options, action) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), UPSTREAM_TIMEOUT_MS);
  try {
    const response = await fetch(url, { ...options, signal: controller.signal });
    if (!response.ok) throw new Error(`${action}失败（HTTP ${response.status}）`);
    const text = await response.text();
    try {
      return JSON.parse(text);
    } catch {
      throw new Error(`${action}返回了无效数据`);
    }
  } catch (error) {
    if (error && error.name === 'AbortError') throw new Error(`${action}超时`);
    throw error;
  } finally {
    clearTimeout(timer);
  }
}

async function accountProfile(credential, fallbackUserId = '') {
  const userId = accountUserId(credential, fallbackUserId);
  if (!credential || !userId) throw new Error('登录凭据缺少 QQ 音乐账号标识');
  const url = new URL('https://c6.y.qq.com/rsc/fcgi-bin/fcg_get_profile_homepage.fcg');
  Object.entries({
    _: Date.now(),
    cv: 4747474,
    ct: 24,
    format: 'json',
    inCharset: 'utf-8',
    outCharset: 'utf-8',
    notice: 0,
    platform: 'yqq.json',
    needNewCode: 0,
    uin: userId,
    g_tk_new_20200303: 0,
    g_tk: 0,
    cid: 205360838,
    userid: userId,
    reqfrom: 1,
    reqtype: 0,
    hostUin: 0,
    loginUin: userId,
  }).forEach(([key, value]) => url.searchParams.set(key, String(value)));
  const raw = await fetchJson(url, {
    headers: {
      Cookie: credential,
      Referer: `https://y.qq.com/portal/profile.html?uin=${encodeURIComponent(userId)}`,
      'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)',
    },
  }, '获取 QQ 音乐账号资料');
  if (Number(raw.code) !== 0) throw new Error(`QQ 音乐账号资料验证失败（业务码 ${raw.code}）`);
  return { raw, userId };
}

async function validateCredential(credential, loginMethod = '') {
  const { raw, userId } = await accountProfile(credential);
  const profile = contract.profilePayload(raw, userId);
  if (!profile.userId || (!profile.avatarUrl && profile.nickname === 'QQ音乐用户'))
    throw new Error('QQ 音乐账号资料不完整，请重新登录');
  const isWechatAccount = Boolean(cookieValue(credential, ['wxuin', 'wx_openid', 'wxopenid']));
  if (!profile.avatarUrl && !isWechatAccount)
    profile.avatarUrl = `https://q1.qlogo.cn/g?b=qq&nk=${encodeURIComponent(profile.userId)}&s=140`;
  return { ...profile, loginMethod };
}

function runAuthWorker(attempt) {
  if (attempt.worker) throw new Error('登录状态请求仍在进行中');
  return new Promise((resolve, reject) => {
    const worker = new Worker(path.join(__dirname, 'auth-worker.js'), {
      workerData: {
        method: attempt.method,
        ptqrtoken: attempt.ptqrtoken,
        qrsig: attempt.qrsig,
        uuid: attempt.uuid,
      },
    });
    attempt.worker = worker;
    const finish = () => {
      if (attempt.worker === worker) attempt.worker = null;
    };
    worker.once('message', (message) => {
      finish();
      worker.terminate();
      if (message.ok) resolve(message.result);
      else reject(new Error(message.error || '登录状态查询失败'));
    });
    worker.once('error', (error) => {
      finish();
      reject(error);
    });
    worker.once('exit', (code) => {
      finish();
      if (code !== 0 && !attempt.canceled) reject(new Error('登录状态任务异常结束'));
    });
  });
}

async function probeQqProtocol(attempt) {
  const controller = new AbortController();
  attempt.controller = controller;
  const timer = setTimeout(() => controller.abort(), 10000);
  try {
    const url = `https://ssl.ptlogin2.qq.com/ptqrlogin?u1=https%3A%2F%2Fgraph.qq.com%2Foauth2.0%2Flogin_jump&ptqrtoken=${encodeURIComponent(attempt.ptqrtoken)}&ptredirect=0&h=1&t=1&g=1&from_ui=1&ptlang=2052&action=0-0-${Date.now()}&js_ver=23111510&js_type=1&pt_uistyle=40&aid=716027609&daid=383&pt_3rd_aid=100497308`;
    const text = await (await fetch(url, { headers: { Cookie: `qrsig=${attempt.qrsig}` }, signal: controller.signal })).text();
    const match = text.match(/ptuiCB\(['"](\d+)['"]/);
    return match ? Number(match[1]) : 66;
  } finally {
    clearTimeout(timer);
    if (attempt.controller === controller) attempt.controller = null;
  }
}

function cancelAttempt(attempt) {
  if (!attempt) return;
  attempt.canceled = true;
  if (attempt.controller) attempt.controller.abort();
  if (attempt.worker) attempt.worker.terminate();
  attempt.controller = null;
  attempt.worker = null;
}

async function startQr(method) {
  if (method !== 'qq' && method !== 'wechat') throw new Error('登录方式必须是 qq 或 wechat');
  const result = method === 'wechat' ? await sdk.getWXLoginQr() : await sdk.getQQLoginQr();
  const body = contract.unwrap(result);
  const qrImage = body.img;
  if (!qrImage || (method === 'wechat' ? !body.uuid : (!body.qrsig || !body.ptqrtoken))) {
    throw new Error('QQ 服务未返回完整二维码会话');
  }
  const id = randomUUID();
  attempts.set(id, {
    id,
    method,
    createdAt: Date.now(),
    qrsig: body.qrsig || '',
    ptqrtoken: body.ptqrtoken ? String(body.ptqrtoken) : '',
    uuid: body.uuid || '',
    canceled: false,
    worker: null,
    controller: null,
  });
  return { loginAttemptId: id, method, qrImage, state: 'WAITING_SCAN' };
}

async function pollAttempt(attempt) {
  if (!attempt || attempt.canceled) throw new Error('登录任务不存在或已取消');
  if (Date.now() - attempt.createdAt > ATTEMPT_TTL_MS) {
    cancelAttempt(attempt);
    return { state: 'EXPIRED', protocolCode: attempt.method === 'wechat' ? 402 : 65, method: attempt.method };
  }
  const result = await runAuthWorker(attempt);
  const body = result && result.body ? result.body : result;
  let mapped = contract.authState(attempt.method, body);
  if (attempt.method === 'qq' && mapped.state === 'WAITING_SCAN') {
    const code = await probeQqProtocol(attempt);
    if (code === 67) mapped = { state: 'WAITING_CONFIRM', protocolCode: 67 };
    else if (code === 65) mapped = { state: 'EXPIRED', protocolCode: 65 };
    else if (code === 68) mapped = { state: 'REFUSED', protocolCode: 68 };
  }
  if (mapped.state !== 'AUTHORIZED') {
    return { ...mapped, method: attempt.method, message: body && body.message ? String(body.message) : '' };
  }
  const session = body.session || {};
  const credential = session.cookie || '';
  const profile = await validateCredential(credential, attempt.method);
  attempt.authorizedAt = Date.now();
  return { ...mapped, method: attempt.method, credential, profile };
}

const searchCategoryConfig = {
  all: { type: 0, remoteplace: 'song' },
  songs: { type: 0, remoteplace: 'song' },
  artists: { type: 1, remoteplace: 'singer' },
  albums: { type: 2, remoteplace: 'album' },
  playlists: { type: 3, remoteplace: 'playlist' },
  lyrics: { type: 7, remoteplace: 'lyric' },
};

async function searchPlaylists(body, limit, page) {
  const keywords = String(body.keywords || '').trim();
  const raw = await fetchJson('https://u.y.qq.com/cgi-bin/musicu.fcg', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Referer: 'https://y.qq.com/',
      'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)',
    },
    body: JSON.stringify({
      comm: { ct: '19', cv: '1859', uin: '0' },
      req: {
        method: 'DoSearchForQQMusicDesktop',
        module: 'music.search.SearchCgiService',
        param: {
          grp: 1,
          num_per_page: limit,
          page_num: page,
          query: keywords,
          search_type: 3,
        },
      },
    }),
  }, 'QQ 歌单搜索');
  const response = raw && raw.req;
  if (!response || Number(response.code) !== 0 || Number(response.data?.code) !== 0)
    throw new Error('QQ 歌单搜索返回了异常业务状态');
  const meta = response.data?.meta || {};
  return {
    items: contract.searchItems(response.data, 'playlists').slice(0, limit),
    hasMore: Number(meta.nextpage || 0) > page
      || Number(meta.sum || 0) > page * limit,
  };
}

async function searchResults(body) {
  const limit = Math.max(1, Math.min(100, Number(body.limit || 30)));
  const offset = Math.max(0, Number(body.offset || 0));
  const page = Math.floor(offset / limit) + 1;
  const category = Object.prototype.hasOwnProperty.call(searchCategoryConfig, body.category)
    ? String(body.category) : 'songs';
  if (category === 'all') {
    const categories = ['songs', 'artists', 'albums', 'playlists', 'lyrics'];
    const categoryLimit = Math.max(3, Math.ceil(limit / categories.length));
    const categoryOffset = Math.floor(offset / limit) * categoryLimit;
    const settled = await Promise.allSettled(categories.map((item) => searchResults({
      ...body,
      category: item,
      limit: categoryLimit,
      offset: categoryOffset,
    })));
    const successful = settled.filter((item) => item.status === 'fulfilled')
      .map((item) => item.value);
    if (!successful.length) {
      const firstFailure = settled.find((item) => item.status === 'rejected');
      throw (firstFailure && firstFailure.reason) || new Error('QQ 综合搜索失败');
    }
    const order = ['artists', 'songs', 'albums', 'playlists', 'lyrics'];
    successful.sort((left, right) => order.indexOf(left.category) - order.indexOf(right.category));
    const items = successful.flatMap((item) => item.items);
    return {
      category,
      items,
      songs: items.filter((item) => item.type === 'song'),
      hasMore: successful.some((item) => item.hasMore),
    };
  }
  const config = searchCategoryConfig[category];
  const keywords = String(body.keywords || '').trim();
  if (!keywords) throw new Error('搜索关键词不能为空');
  if (category === 'playlists') {
    const result = await searchPlaylists(body, limit, page);
    return {
      category,
      items: result.items,
      songs: [],
      hasMore: result.hasMore,
    };
  }
  const raw = contract.unwrap(await services.getSearchByKey({
    method: 'get',
    params: {
      w: keywords,
      n: limit,
      p: page,
      t: config.type,
      catZhida: 1,
      remoteplace: 'txt.yqq.' + config.remoteplace,
    },
    option: {},
  }));
  const items = contract.searchItems(raw, category).slice(0, limit);
  return {
    category,
    items,
    songs: items.filter((item) => item.type === 'song'),
    hasMore: items.length >= limit,
  };
}

async function searchHotTerms(body) {
  const limit = Math.max(1, Math.min(50, Number(body.limit || 20)));
  const raw = contract.unwrap(await services.getHotKey({
    method: 'get',
    params: {},
    option: {},
  }));
  return contract.hotKeys(raw).slice(0, limit);
}

async function searchSuggestions(body) {
  const limit = Math.max(1, Math.min(30, Number(body.limit || 10)));
  const keywords = String(body.keywords || '').trim();
  if (!keywords) return [];
  const raw = contract.unwrap(await services.getSmartbox({
    method: 'get',
    params: { key: keywords },
    option: {},
  }));
  return contract.suggestions(raw, limit);
}

async function mediaAddresses(body) {
  const ids = Array.isArray(body.ids) ? body.ids.map(String).filter(Boolean).slice(0, 100) : [];
  const credential = String(body.credential || '');
  const data = [];
  for (const remoteId of ids) {
    try {
      const raw = contract.unwrap(await sdk.getMusicPlay({ songmid: remoteId, quality: '128', credential, cookie: credential }));
      data.push({ remoteId, url: contract.collectUrls(raw)[0] || '', error: '' });
    } catch (error) {
      data.push({ remoteId, url: '', error: error.message || '获取播放地址失败' });
    }
  }
  return data;
}

async function lyrics(body) {
  const raw = contract.unwrap(await sdk.getLyric({
    songmid: String(body.remoteId || ''),
    isFormat: false,
    cookie: String(body.credential || ''),
  }));
  return contract.lyricPayload(raw);
}

async function playlistDetail(body) {
  const raw = contract.unwrap(await services.songListDetail({
    method: 'get',
    params: { disstid: String(body.remoteId || ''), type: 1, json: 1 },
    option: {},
  }));
  const playlist = contract.collectPlaylists(raw)[0] || {
    remoteId: String(body.remoteId || ''),
    name: findText(raw, ['dissname', 'title', 'name']),
    coverUrl: '',
  };
  return { ...playlist, songs: contract.collectSongs(raw) };
}

async function topPlaylists(body) {
  const offset = Math.max(0, Number(body.offset || 0));
  const limit = Math.max(1, Math.min(50, Number(body.limit || 20)));
  const raw = contract.unwrap(await services.songLists({
    method: 'get',
    params: {
      categoryId: Number(body.categoryId || 10000000),
      sortId: 5,
      sin: offset,
      ein: offset + limit - 1,
    },
    option: {},
  }));
  return contract.collectPlaylists(raw).slice(0, limit);
}

async function albumDetail(body) {
  const remoteId = String(body.remoteId || '');
  if (!remoteId) throw new Error('缺少 QQ 专辑 MID');
  const [infoResult, songsResult] = await Promise.all([
    services.getAlbumInfo({ method: 'get', params: { albummid: remoteId }, option: {} }),
    services.getAlbumSongs({
      method: 'POST',
      params: { albummid: remoteId, begin: 0, num: 999 },
      option: {},
    }),
  ]);
  return contract.albumPayload({
    info: contract.unwrap(infoResult),
    songs: contract.unwrap(songsResult),
  }, remoteId);
}

async function artistBundle(remoteId) {
  if (!remoteId) throw new Error('缺少 QQ 歌手 MID');
  const data = {
    comm: { ct: 24, cv: 0 },
    singer: {
      method: 'get_singer_detail_info',
      param: { sort: 5, singermid: remoteId, sin: 0, num: 200 },
      module: 'music.web_singer_info_svr',
    },
  };
  return contract.unwrap(await services.UCommon({
    method: 'get',
    params: { format: 'json', singermid: remoteId, data: JSON.stringify(data) },
    option: {},
  }));
}

async function artistDetail(body) {
  const remoteId = String(body.remoteId || '');
  return contract.artistPayload(await artistBundle(remoteId), remoteId);
}

async function accountPlaylists(body) {
  const credential = String(body.credential || '');
  const { raw } = await accountProfile(credential, String(body.userId || ''));
  const offset = Math.max(0, Number(body.offset || 0));
  const limit = Math.max(1, Math.min(100, Number(body.limit || 50)));
  return contract.collectPlaylists(raw).slice(offset, offset + limit);
}

async function recommendSongs(credential) {
  try {
    const personalized = contract.collectSongs(
      contract.unwrap(await services.getDailyRecommend(credential)),
    );
    if (personalized.length) return personalized.slice(0, 30);
  } catch {
    // QQ 的旧个性化接口会按账号类型和灰度状态失效；继续使用来源内回退。
  }
  const fallback = contract.collectSongs(contract.unwrap(await services.getNewSongs(1, 30)));
  if (!fallback.length) throw new Error('QQ 音乐暂未返回可用推荐歌曲');
  return fallback.slice(0, 30);
}

function findText(value, keys) {
  let result = '';
  const seen = new Set();
  function visit(node) {
    if (result || !node || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    result = keys.map((key) => node[key]).find((item) => typeof item === 'string' && item.trim()) || '';
    if (!result) Object.values(node).forEach(visit);
  }
  visit(value);
  return result;
}

async function route(request, response) {
  const url = new URL(request.url, `http://${HOST}:${PORT}`);
  if (request.method === 'GET' && url.pathname === '/health') {
    return success(response, { service: SERVICE, version: '1.0.0', upstream: '@sansenjian/qq-music-api@2.6.0' });
  }
  if (request.method !== 'POST') return failure(response, 404, 'NOT_FOUND', '接口不存在');
  const body = await readJson(request);
  if (url.pathname === '/auth/qr/start') return success(response, await startQr(String(body.method || 'qq')));
  if (url.pathname === '/auth/qr/status') {
    const attempt = attempts.get(String(body.loginAttemptId || ''));
    if (!attempt) return failure(response, 404, 'ATTEMPT_NOT_FOUND', '登录任务不存在或已结束');
    return success(response, await pollAttempt(attempt));
  }
  if (url.pathname === '/auth/qr/cancel') {
    const id = String(body.loginAttemptId || '');
    cancelAttempt(attempts.get(id));
    attempts.delete(id);
    return success(response, { canceled: true });
  }
  if (url.pathname === '/auth/validate') return success(response, await validateCredential(String(body.credential || ''), String(body.loginMethod || 'saved')));
  if (url.pathname === '/auth/logout') return success(response, { loggedOut: true });
  if (url.pathname === '/v1/account/playlists') {
    return success(response, { playlists: await accountPlaylists(body) });
  }
  if (url.pathname === '/v1/search') return success(response, await searchResults(body));
  if (url.pathname === '/v1/search/hot') {
    return success(response, { terms: await searchHotTerms(body) });
  }
  if (url.pathname === '/v1/search/suggest') {
    return success(response, { suggestions: await searchSuggestions(body) });
  }
  if (url.pathname === '/v1/media') return success(response, { addresses: await mediaAddresses(body) });
  if (url.pathname === '/v1/lyrics') return success(response, await lyrics(body));
  if (url.pathname === '/v1/playlist/detail') return success(response, await playlistDetail(body));
  if (url.pathname === '/v1/playlists') return success(response, { playlists: await topPlaylists(body) });
  if (url.pathname === '/v1/album/detail') return success(response, await albumDetail(body));
  if (url.pathname === '/v1/artist/detail') return success(response, await artistDetail(body));
  if (url.pathname === '/v1/artist/songs') {
    const remoteId = String(body.remoteId || '');
    return success(response, { songs: contract.collectSongs(await artistBundle(remoteId)) });
  }
  if (url.pathname === '/v1/toplists') {
    const raw = contract.unwrap(await services.getTopLists({ method: 'get', params: {}, option: {} }));
    return success(response, { items: raw });
  }
  if (url.pathname === '/v1/recommend') {
    return success(response, { songs: await recommendSongs(String(body.credential || '')) });
  }
  return failure(response, 404, 'NOT_FOUND', '接口不存在');
}

const server = http.createServer((request, response) => {
  route(request, response).catch((error) => {
    failure(response, 502, 'UPSTREAM_ERROR', error && error.message ? error.message : 'QQ 服务请求失败');
  });
});

const cleanup = setInterval(() => {
  const now = Date.now();
  for (const [id, attempt] of attempts) {
    const reference = attempt.authorizedAt || attempt.createdAt;
    if (now - reference > ATTEMPT_TTL_MS) {
      cancelAttempt(attempt);
      attempts.delete(id);
    }
  }
}, 30000);
cleanup.unref();

server.listen(PORT, HOST, () => {
  process.stdout.write(`${SERVICE} listening on http://${HOST}:${PORT}\n`);
});

function shutdown() {
  for (const attempt of attempts.values()) cancelAttempt(attempt);
  attempts.clear();
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 1500).unref();
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
