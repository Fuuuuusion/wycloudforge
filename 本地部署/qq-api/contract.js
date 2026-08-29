'use strict';

function unwrap(result) {
  if (!result || typeof result !== 'object') {
    throw new Error('QQ 服务返回了无效响应');
  }
  if (Number(result.status || 200) >= 400) {
    throw new Error(errorText(result.body) || `QQ 服务请求失败（HTTP ${result.status}）`);
  }
  const body = result.body && typeof result.body === 'object' ? result.body : result;
  if (body.error !== undefined && body.error !== null && body.error !== '') {
    throw new Error(errorText(body.error) || 'QQ 服务请求失败');
  }
  if (Object.prototype.hasOwnProperty.call(body, 'response')) return body.response;
  if (Object.prototype.hasOwnProperty.call(body, 'data')) return body.data;
  return body;
}

function errorText(value) {
  if (typeof value === 'string') return value;
  if (value && typeof value.message === 'string') return value.message;
  try {
    return value === undefined ? '' : JSON.stringify(value);
  } catch {
    return '';
  }
}

function firstString(object, keys) {
  if (!object || typeof object !== 'object') return '';
  for (const key of keys) {
    const value = object[key];
    if (typeof value === 'string' && value.trim()) return value.trim();
    if ((typeof value === 'number' || typeof value === 'bigint') && String(value)) return String(value);
  }
  return '';
}

function normalizeSong(raw) {
  if (!raw || typeof raw !== 'object') return null;
  const explicitSongMid = firstString(raw, ['songmid', 'song_mid', 'songMid']);
  const hasSongShape = ['songname', 'song_name', 'interval', 'singer', 'singers', 'albumname']
    .some((key) => Object.prototype.hasOwnProperty.call(raw, key));
  const remoteId = explicitSongMid || (hasSongShape ? firstString(raw, ['mid']) : '');
  if (!remoteId) return null;
  const albumObject = raw.album && typeof raw.album === 'object' ? raw.album : {};
  const singerValue = raw.singer || raw.singers || raw.artist || [];
  const singers = Array.isArray(singerValue) ? singerValue : [singerValue];
  const artistNames = singers.map((item) => typeof item === 'string'
    ? item
    : firstString(item, ['name', 'title', 'singername', 'singer_name'])).filter(Boolean);
  const artistRemoteId = singers.map((item) => firstString(item, ['mid', 'singermid', 'singer_mid', 'id']))
    .find(Boolean) || firstString(raw, ['singermid', 'singer_mid']);
  const albumRemoteId = firstString(raw, ['albummid', 'album_mid', 'albumMid'])
    || firstString(albumObject, ['mid', 'albummid', 'album_mid']);
  const title = firstString(raw, ['songname', 'song_name', 'name', 'title']) || remoteId;
  const album = firstString(raw, ['albumname', 'album_name'])
    || firstString(albumObject, ['name', 'title']);
  let durationMs = Number(raw.duration_ms || raw.durationMs || 0);
  if (!durationMs) {
    const seconds = Number(raw.interval || raw.duration || 0);
    durationMs = Number.isFinite(seconds) ? Math.round(seconds * 1000) : 0;
  }
  const coverUrl = firstString(raw, ['coverUrl', 'cover_url', 'picurl', 'picUrl'])
    || (albumRemoteId
      ? `https://y.gtimg.cn/music/photo_new/T002R300x300M000${encodeURIComponent(albumRemoteId)}.jpg?max_age=2592000`
      : '');
  return {
    remoteId,
    title,
    artist: artistNames.join('/'),
    artistRemoteId,
    album,
    albumRemoteId,
    durationMs,
    coverUrl,
  };
}

function collectSongs(value) {
  const found = new Map();
  const seen = new Set();
  function visit(node) {
    if (!node || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    const song = normalizeSong(node);
    if (song && !found.has(song.remoteId)) found.set(song.remoteId, song);
    if (Array.isArray(node)) {
      node.forEach(visit);
      return;
    }
    Object.values(node).forEach(visit);
  }
  visit(value);
  return Array.from(found.values());
}

function normalizePlaylist(raw) {
  if (!raw || typeof raw !== 'object') return null;
  const hasPlaylistShape = ['dissname', 'songnum', 'listennum', 'imgurl', 'logo']
    .some((key) => Object.prototype.hasOwnProperty.call(raw, key));
  const remoteId = firstString(raw, ['dissid', 'disstid', 'playlistId'])
    || (hasPlaylistShape ? firstString(raw, ['id', 'tid']) : '');
  if (!remoteId) return null;
  const name = firstString(raw, ['dissname', 'name', 'title']) || remoteId;
  const coverUrl = firstString(raw, [
    'imgurl', 'logo', 'picUrl', 'picurl', 'coverUrl', 'cover_url', 'cover',
  ]);
  return { remoteId, name, coverUrl };
}

function collectPlaylists(value) {
  const found = new Map();
  const seen = new Set();
  function visit(node) {
    if (!node || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    const playlist = normalizePlaylist(node);
    if (playlist && !found.has(playlist.remoteId)) found.set(playlist.remoteId, playlist);
    if (Array.isArray(node)) node.forEach(visit);
    else Object.values(node).forEach(visit);
  }
  visit(value);
  return Array.from(found.values());
}

function findObject(value, predicate) {
  let result = null;
  const seen = new Set();
  function visit(node) {
    if (result || !node || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    if (!Array.isArray(node) && predicate(node)) {
      result = node;
      return;
    }
    Object.values(node).forEach(visit);
  }
  visit(value);
  return result;
}

function albumPayload(value, fallbackRemoteId = '') {
  const albumObject = findObject(value, (node) => {
    const remoteId = firstString(node, ['albummid', 'album_mid', 'albumMid']);
    return remoteId && (!fallbackRemoteId || remoteId === String(fallbackRemoteId));
  }) || {};
  const remoteId = firstString(albumObject, ['albummid', 'album_mid', 'albumMid'])
    || String(fallbackRemoteId || '');
  const name = firstString(albumObject, ['albumname', 'album_name', 'name', 'title']);
  const coverUrl = firstString(albumObject, ['picUrl', 'picurl', 'coverUrl', 'cover_url'])
    || (remoteId
      ? `https://y.gtimg.cn/music/photo_new/T002R300x300M000${encodeURIComponent(remoteId)}.jpg?max_age=2592000`
      : '');
  return {
    album: { remoteId, name, picUrl: coverUrl, coverUrl },
    songs: collectSongs(value),
  };
}

function artistPayload(value, fallbackRemoteId = '') {
  const artistObject = findObject(value, (node) => {
    const hasArtistName = ['singer_name', 'singername', 'singerName']
      .some((key) => Object.prototype.hasOwnProperty.call(node, key));
    if (!hasArtistName) return false;
    const remoteId = firstString(node, ['singer_mid', 'singermid', 'singerMid', 'mid']);
    return remoteId && (!fallbackRemoteId || remoteId === String(fallbackRemoteId));
  }) || {};
  const remoteId = firstString(artistObject, ['singer_mid', 'singermid', 'singerMid', 'mid'])
    || String(fallbackRemoteId || '');
  const name = firstString(artistObject, ['singer_name', 'singername', 'singerName', 'name', 'title']);
  const coverUrl = firstString(artistObject, ['picUrl', 'picurl', 'avatarUrl', 'photo'])
    || (remoteId
      ? `https://y.gtimg.cn/music/photo_new/T001R300x300M000${encodeURIComponent(remoteId)}.jpg?max_age=2592000`
      : '');
  return {
    artist: { remoteId, name, picUrl: coverUrl, coverUrl },
    songs: collectSongs(value),
  };
}

function collectUrls(value) {
  const urls = [];
  const seen = new Set();
  function visit(node, key = '') {
    if (typeof node === 'string') {
      if (/^https?:\/\//i.test(node) && (/(url|purl|play)/i.test(key) || /\.(m4a|mp3|flac|ogg)(\?|$)/i.test(node))) {
        urls.push(node);
      }
      return;
    }
    if (!node || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    if (Array.isArray(node)) node.forEach((item) => visit(item, key));
    else Object.entries(node).forEach(([childKey, child]) => visit(child, childKey));
  }
  visit(value);
  return urls;
}

function lyricPayload(value) {
  let original = '';
  let translated = '';
  let romanized = '';
  const seen = new Set();
  function visit(node) {
    if (!node || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    for (const [key, child] of Object.entries(node)) {
      if (typeof child === 'string') {
        if (!original && /^(lyric|lrc)$/i.test(key)) original = child;
        else if (!translated && /^(trans|translyric|tlyric)$/i.test(key)) translated = child;
        else if (!romanized && /^(roma|romalrc|romalyric)$/i.test(key)) romanized = child;
      } else {
        visit(child);
      }
    }
  }
  visit(value);
  return { original, translated, romanized };
}

function profilePayload(value, fallbackUserId = '') {
  let userId = String(fallbackUserId || '');
  let nickname = '';
  let avatarUrl = '';
  const seen = new Set();
  function visit(node) {
    if (!node || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    if (!userId) userId = firstString(node, ['uin', 'musicid', 'userId', 'userid', 'id']);
    if (!nickname) nickname = firstString(node, ['nickname', 'nick', 'name', 'creator_name']);
    if (!avatarUrl) avatarUrl = firstString(node, ['avatarUrl', 'avatar', 'headpic', 'picurl']);
    Object.values(node).forEach(visit);
  }
  visit(value);
  return { userId, nickname: nickname || 'QQ音乐用户', avatarUrl };
}

function authState(method, body) {
  if (body && body.isOk && body.session) return { state: 'AUTHORIZED', protocolCode: method === 'wechat' ? 405 : 0 };
  if (body && body.refused) return { state: 'REFUSED', protocolCode: method === 'wechat' ? 403 : 68 };
  if (body && body.refresh) return { state: 'EXPIRED', protocolCode: method === 'wechat' ? 402 : 65 };
  if (body && body.scanned) return { state: 'WAITING_CONFIRM', protocolCode: method === 'wechat' ? 404 : 67 };
  return { state: 'WAITING_SCAN', protocolCode: method === 'wechat' ? 408 : 66 };
}

module.exports = {
  albumPayload,
  artistPayload,
  authState,
  collectPlaylists,
  collectSongs,
  collectUrls,
  errorText,
  lyricPayload,
  normalizeSong,
  normalizePlaylist,
  profilePayload,
  unwrap,
};
