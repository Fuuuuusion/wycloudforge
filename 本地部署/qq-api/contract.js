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
  const fileObject = raw.file && typeof raw.file === 'object' ? raw.file : {};
  const mediaRemoteId = firstString(raw, [
    'mediaRemoteId', 'media_mid', 'mediaMid', 'strMediaMid', 'strMediaId',
  ]) || firstString(fileObject, [
    'media_mid', 'mediaMid', 'strMediaMid', 'strMediaId', 'mediaid',
  ]);
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
    mediaRemoteId,
    title,
    artist: artistNames.join('/'),
    artistRemoteId,
    album,
    albumRemoteId,
    durationMs,
    coverUrl,
  };
}

function mediaAddress(value, remoteId) {
  const id = String(remoteId || '');
  const root = value && typeof value === 'object' ? value : {};
  const playUrl = root.playUrl && typeof root.playUrl === 'object' ? root.playUrl : {};
  const entry = playUrl[id] && typeof playUrl[id] === 'object' ? playUrl[id] : root;
  const url = firstString(entry, ['url', 'playUrl', 'purl']) || collectUrls(entry)[0] || '';
  const error = firstString(entry, ['error', 'message', 'msg']);
  return { remoteId: id, url, error };
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

function lyricSnippets(value) {
  const found = new Map();
  const seen = new Set();
  function visit(node) {
    if (!node || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    const remoteId = firstString(node, ['songmid', 'song_mid', 'songMid']);
    if (remoteId && !found.has(remoteId)) {
      const raw = firstString(node, ['lyric']);
      const firstLine = raw.split(/\\n|\r?\n/).map((line) => line.trim()).find(Boolean) || '';
      const text = firstLine.replace(/<[^>]+>/g, '').trim();
      if (text) found.set(remoteId, text);
    }
    if (Array.isArray(node)) node.forEach(visit);
    else Object.values(node).forEach(visit);
  }
  visit(value);
  return found;
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
  const creator = firstString(raw, ['creatorName', 'nickname'])
    || firstString(raw.creator, ['name', 'nickname']);
  const popularityValue = raw.listennum ?? raw.listenCount ?? raw.playCount ?? -1;
  const popularity = Number(popularityValue);
  const playlist = { remoteId, name, coverUrl };
  if (creator) playlist.creator = creator;
  if (Number.isFinite(popularity) && popularity >= 0) playlist.popularity = popularity;
  return playlist;
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

function normalizeArtist(raw) {
  if (!raw || typeof raw !== 'object') return null;
  const explicitRemoteId = firstString(raw, [
    'singermid', 'singer_mid', 'singerMid', 'singerMID',
  ]);
  const hasArtistShape = [
    'singername', 'singer_name', 'singerName', 'singerMID', 'singer_pic', 'singerPic',
  ].some((key) => Object.prototype.hasOwnProperty.call(raw, key));
  const remoteId = explicitRemoteId || (hasArtistShape ? firstString(raw, ['mid', 'id']) : '');
  if (!remoteId) return null;
  const name = firstString(raw, [
    'singername', 'singer_name', 'singerName', 'name', 'title',
  ]);
  if (!name) return null;
  const coverUrl = firstString(raw, [
    'singer_pic', 'singerPic', 'picUrl', 'picurl', 'avatarUrl', 'photo',
  ]) || 'https://y.gtimg.cn/music/photo_new/T001R300x300M000'
    + encodeURIComponent(remoteId) + '.jpg?max_age=2592000';
  return { remoteId, name, coverUrl };
}

function collectArtists(value) {
  const found = new Map();
  const seen = new Set();
  function visit(node) {
    if (!node || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    const artist = normalizeArtist(node);
    if (artist && !found.has(artist.remoteId)) found.set(artist.remoteId, artist);
    if (Array.isArray(node)) node.forEach(visit);
    else Object.values(node).forEach(visit);
  }
  visit(value);
  return Array.from(found.values());
}

function normalizeAlbum(raw) {
  if (!raw || typeof raw !== 'object') return null;
  const explicitRemoteId = firstString(raw, [
    'albummid', 'album_mid', 'albumMid', 'albumMID',
  ]);
  const hasAlbumShape = [
    'albumname', 'album_name', 'albumName', 'albumMID', 'publicTime', 'pub_time',
  ].some((key) => Object.prototype.hasOwnProperty.call(raw, key));
  const remoteId = explicitRemoteId || (hasAlbumShape ? firstString(raw, ['mid', 'id']) : '');
  if (!remoteId) return null;
  const name = firstString(raw, [
    'albumname', 'album_name', 'albumName', 'name', 'title',
  ]);
  if (!name) return null;
  const singerValue = raw.singer || raw.singers || raw.artist || [];
  const singers = Array.isArray(singerValue) ? singerValue : [singerValue];
  const artist = firstString(raw, ['singername', 'singer_name', 'singerName', 'artistName'])
    || singers.map((item) => typeof item === 'string'
      ? item
      : firstString(item, ['name', 'title', 'singername', 'singer_name'])).filter(Boolean).join('/');
  const artistRemoteId = firstString(raw, [
    'singermid', 'singer_mid', 'singerMid', 'singerMID',
  ]) || singers.map((item) => firstString(item, ['mid', 'singermid', 'singer_mid', 'id']))
    .find(Boolean) || '';
  const coverUrl = firstString(raw, ['coverUrl', 'cover_url', 'picUrl', 'picurl'])
    || 'https://y.gtimg.cn/music/photo_new/T002R300x300M000'
      + encodeURIComponent(remoteId) + '.jpg?max_age=2592000';
  return { remoteId, name, artist, artistRemoteId, coverUrl };
}

function collectAlbums(value) {
  const found = new Map();
  const seen = new Set();
  function visit(node) {
    if (!node || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    const album = normalizeAlbum(node);
    if (album && !found.has(album.remoteId)) found.set(album.remoteId, album);
    if (Array.isArray(node)) node.forEach(visit);
    else Object.values(node).forEach(visit);
  }
  visit(value);
  return Array.from(found.values());
}

function searchItems(value, category = 'songs') {
  const type = String(category || 'songs').toLowerCase();
  if (type === 'songs' || type === 'lyrics') {
    const snippets = type === 'lyrics' ? lyricSnippets(value) : new Map();
    return collectSongs(value).map((song) => {
      const item = {
        type: type === 'lyrics' ? 'lyric' : 'song',
        ...song,
      };
      if (type === 'lyrics') item.subtitle = snippets.get(song.remoteId) || song.artist;
      return item;
    });
  }
  if (type === 'artists') {
    return collectArtists(value).map((artist) => ({
      type: 'artist',
      remoteId: artist.remoteId,
      title: artist.name,
      subtitle: '',
      coverUrl: artist.coverUrl,
    }));
  }
  if (type === 'albums') {
    return collectAlbums(value).map((album) => ({
      type: 'album',
      remoteId: album.remoteId,
      title: album.name,
      subtitle: album.artist,
      artist: album.artist,
      artistRemoteId: album.artistRemoteId,
      coverUrl: album.coverUrl,
    }));
  }
  if (type === 'playlists') {
    return collectPlaylists(value).map((playlist) => {
      const item = {
        type: 'playlist',
        remoteId: playlist.remoteId,
        title: playlist.name,
        subtitle: playlist.creator || '',
        coverUrl: playlist.coverUrl,
      };
      if (Number.isFinite(playlist.popularity)) item.popularity = playlist.popularity;
      return item;
    });
  }
  return [
    ...searchItems(value, 'artists'),
    ...searchItems(value, 'songs'),
    ...searchItems(value, 'albums'),
    ...searchItems(value, 'playlists'),
  ];
}

function hotKeys(value) {
  const result = [];
  const seenText = new Set();
  const seenNodes = new Set();
  function visit(node, parentKey = '') {
    if (!node || typeof node !== 'object' || seenNodes.has(node)) return;
    seenNodes.add(node);
    if (Array.isArray(node)) {
      if (/hot/i.test(parentKey)) {
        for (const item of node) {
          if (!item || typeof item !== 'object') continue;
          const text = firstString(item, ['k', 'keyword', 'searchWord', 'name', 'title']);
          if (!text || seenText.has(text)) continue;
          seenText.add(text);
          result.push({
            text,
            description: firstString(item, ['d', 'description', 'desc', 'content']),
            score: Number(item.n || item.score || item.listenCount || -1),
          });
        }
      }
      node.forEach((item) => visit(item, parentKey));
      return;
    }
    Object.entries(node).forEach(([key, child]) => visit(child, key));
  }
  visit(value);
  return result;
}

function suggestions(value, limit = 10) {
  const result = [];
  const seen = new Set();
  function append(type, remoteId, text, subtitle = '') {
    const normalizedText = String(text || '').trim();
    const key = type + ':' + normalizedText;
    if (!normalizedText || seen.has(key) || result.length >= limit) return;
    seen.add(key);
    result.push({
      type,
      remoteId: String(remoteId || ''),
      text: normalizedText,
      subtitle: String(subtitle || ''),
    });
  }
  collectArtists(value).forEach((artist) => append('artist', artist.remoteId, artist.name));
  collectSongs(value).forEach((song) => append('song', song.remoteId, song.title, song.artist));
  collectAlbums(value).forEach((album) => append('album', album.remoteId, album.name, album.artist));
  collectPlaylists(value).forEach((playlist) => append('playlist', playlist.remoteId, playlist.name));
  return result.slice(0, Math.max(0, limit));
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
  const profile = findObject(value, (node) => [
    'nickname', 'nick', 'creator_name', 'headpic', 'avatarUrl', 'uin_web', 'encrypt_uin',
  ].some((key) => Object.prototype.hasOwnProperty.call(node, key))) || {};
  const userId = String(fallbackUserId || '') || firstString(profile, [
    'uin_web', 'musicid', 'userId', 'userid', 'uin',
  ]);
  const nickname = firstString(profile, ['nickname', 'nick', 'creator_name', 'name']);
  const avatarUrl = firstString(profile, ['avatarUrl', 'avatar', 'headpic', 'picurl']);
  return { userId, nickname: nickname || 'QQ音乐用户', avatarUrl };
}

function vipPayload(value) {
  let recognized = false;
  let active = false;
  let level = 0;
  let expiresAt = 0;
  const seen = new Set();
  function visit(node) {
    if (!node || typeof node !== 'object' || seen.has(node)) return;
    seen.add(node);
    for (const [key, child] of Object.entries(node)) {
      const normalized = key.toLowerCase().replace(/[^a-z0-9]/g, '');
      if (child !== null && typeof child !== 'object') {
        const numeric = Number(child);
        if (/(isvip|vipflag|vipopen|greenvip|supervip|issvip)/.test(normalized)
            && Number.isFinite(numeric)) {
          recognized = true;
          active = active || numeric > 0;
        }
        if (/(viplevel|level)/.test(normalized) && Number.isFinite(numeric)) {
          recognized = true;
          level = Math.max(level, numeric);
        }
        if (/(vipend|expire|endtime)/.test(normalized) && Number.isFinite(numeric)) {
          recognized = true;
          const timestamp = numeric > 100000000000 ? numeric : numeric * 1000;
          expiresAt = Math.max(expiresAt, timestamp);
        }
      } else {
        visit(child);
      }
    }
  }
  visit(value);
  if (expiresAt > Date.now()) active = true;
  return { recognized, active, level, expiresAt };
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
  collectAlbums,
  collectArtists,
  collectPlaylists,
  collectSongs,
  collectUrls,
  errorText,
  hotKeys,
  lyricPayload,
  mediaAddress,
  normalizeAlbum,
  normalizeArtist,
  normalizeSong,
  normalizePlaylist,
  profilePayload,
  vipPayload,
  searchItems,
  suggestions,
  unwrap,
};
