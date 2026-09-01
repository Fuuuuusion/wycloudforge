'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const contract = require('../contract');

test('normalizes fixture and ignores unknown fields', () => {
  const fixture = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures/search-response.json'), 'utf8'));
  const songs = contract.collectSongs(fixture);
  assert.equal(songs.length, 1);
  assert.deepEqual(songs[0], {
    remoteId: '0039MnYb0qxYhV',
    mediaRemoteId: 'MEDIA_DIFFERENT_FROM_SONG_MID',
    title: '测试歌曲',
    artist: '测试歌手',
    artistRemoteId: '0025NhlN2yWrP4',
    album: '测试专辑',
    albumRemoteId: '004DABuD2r4V8n',
    durationMs: 245000,
    coverUrl: 'https://y.gtimg.cn/music/photo_new/T002R300x300M000004DABuD2r4V8n.jpg?max_age=2592000',
  });
});

test('preserves QQ media identity and upstream playback errors', () => {
  assert.deepEqual(contract.mediaAddress({ playUrl: {
    SONG_MID: { url: '', error: 'Cookie 已失效或 uin 缺失' },
  } }, 'SONG_MID'), {
    remoteId: 'SONG_MID',
    url: '',
    error: 'Cookie 已失效或 uin 缺失',
  });
  assert.deepEqual(contract.mediaAddress({ playUrl: {
    SONG_MID: { url: 'https://example.test/song.mp3' },
  } }, 'SONG_MID'), {
    remoteId: 'SONG_MID',
    url: 'https://example.test/song.mp3',
    error: '',
  });
});

test('maps all QQ and WeChat QR states', () => {
  assert.deepEqual(contract.authState('qq', {}), { state: 'WAITING_SCAN', protocolCode: 66 });
  assert.deepEqual(contract.authState('qq', { scanned: true }), { state: 'WAITING_CONFIRM', protocolCode: 67 });
  assert.deepEqual(contract.authState('qq', { isOk: true, session: {} }), { state: 'AUTHORIZED', protocolCode: 0 });
  assert.deepEqual(contract.authState('qq', { refresh: true }), { state: 'EXPIRED', protocolCode: 65 });
  assert.deepEqual(contract.authState('qq', { refused: true }), { state: 'REFUSED', protocolCode: 68 });
  assert.deepEqual(contract.authState('wechat', {}), { state: 'WAITING_SCAN', protocolCode: 408 });
  assert.deepEqual(contract.authState('wechat', { scanned: true }), { state: 'WAITING_CONFIRM', protocolCode: 404 });
  assert.deepEqual(contract.authState('wechat', { isOk: true, session: {} }), { state: 'AUTHORIZED', protocolCode: 405 });
  assert.deepEqual(contract.authState('wechat', { refresh: true }), { state: 'EXPIRED', protocolCode: 402 });
  assert.deepEqual(contract.authState('wechat', { refused: true }), { state: 'REFUSED', protocolCode: 403 });
});

test('keeps large QQ music id as text', () => {
  const profile = contract.profilePayload({ musicid: '9223372036854775808123', nickname: '微信用户' });
  assert.equal(profile.userId, '9223372036854775808123');
});

test('normalizes QQ VIP entitlement without relying on profile login', () => {
  assert.deepEqual(contract.vipPayload({ req_1: { data: {
    is_vip: 1,
    vip_level: 5,
    vip_end_time: 4102444800,
  } } }), {
    recognized: true,
    active: true,
    level: 5,
    expiresAt: 4102444800000,
  });
  assert.deepEqual(contract.vipPayload({ req_1: { data: { is_vip: 0 } } }), {
    recognized: true,
    active: false,
    level: 0,
    expiresAt: 0,
  });
});

test('normalizes WeChat profile and cloud playlists without narrowing musicid', () => {
  const raw = {
    code: 0,
    data: {
      creator: {
        uin: 0,
        uin_web: '1234567890123456789',
        nick: '微信昵称',
        headpic: 'https://example.test/wechat-avatar.jpg',
      },
      mydiss: {
        list: [{
          dissid: '9223372036854775808124',
          title: '微信账号歌单',
          picurl: 'https://example.test/playlist.jpg',
        }],
      },
    },
  };
  assert.deepEqual(contract.profilePayload(raw, '1234567890123456789'), {
    userId: '1234567890123456789',
    nickname: '微信昵称',
    avatarUrl: 'https://example.test/wechat-avatar.jpg',
  });
  assert.deepEqual(contract.collectPlaylists(raw), [{
    remoteId: '9223372036854775808124',
    name: '微信账号歌单',
    coverUrl: 'https://example.test/playlist.jpg',
  }]);
});

test('normalizes current QQ new-song response used by recommendation fallback', () => {
  const songs = contract.collectSongs({ new_song: { data: { songlist: [{
    mid: '003CURRENTSONGMID',
    name: '新歌推荐',
    interval: 201,
    singer: [{ mid: '002CURRENTARTIST', name: '推荐歌手' }],
    album: { mid: '004CURRENTALBUM', name: '推荐专辑' },
  }] } } });
  assert.equal(songs.length, 1);
  assert.equal(songs[0].remoteId, '003CURRENTSONGMID');
  assert.equal(songs[0].mediaRemoteId, '');
  assert.equal(songs[0].durationMs, 201000);
  assert.equal(songs[0].artist, '推荐歌手');
});

test('normalizes playlist, album and artist identities as text', () => {
  const playlists = contract.collectPlaylists({ data: {
    list: [{ dissid: '9223372036854775808124', dissname: '测试歌单', imgurl: 'https://example.test/p.jpg' }],
  } });
  assert.deepEqual(playlists, [{
    remoteId: '9223372036854775808124',
    name: '测试歌单',
    coverUrl: 'https://example.test/p.jpg',
  }]);
  const album = contract.albumPayload({ albummid: 'ALBUM_MID', albumname: '测试专辑' }, 'ALBUM_MID');
  assert.equal(album.album.remoteId, 'ALBUM_MID');
  assert.equal(album.album.name, '测试专辑');
  const artist = contract.artistPayload({ singer_mid: 'ARTIST_MID', singer_name: '测试歌手' }, 'ARTIST_MID');
  assert.equal(artist.artist.remoteId, 'ARTIST_MID');
  assert.equal(artist.artist.name, '测试歌手');
});

test('normalizes categorized search results into stable typed items', () => {
  const fixture = JSON.parse(fs.readFileSync(
    path.join(__dirname, 'fixtures/search-categories-response.json'), 'utf8',
  ));
  const songs = contract.searchItems(fixture, 'songs');
  const lyrics = contract.searchItems(fixture, 'lyrics');
  const artists = contract.searchItems(fixture, 'artists');
  const albums = contract.searchItems(fixture, 'albums');
  const playlists = contract.searchItems(fixture, 'playlists');
  assert.equal(songs.length, 1);
  assert.equal(songs[0].type, 'song');
  assert.equal(songs[0].remoteId, 'SONG_MID');
  assert.equal(lyrics.length, 1);
  assert.equal(lyrics[0].type, 'lyric');
  assert.equal(lyrics[0].subtitle, '命中的歌词片段');
  assert.deepEqual(artists, [{
    type: 'artist',
    remoteId: 'ARTIST_MID',
    title: '分类测试歌手',
    subtitle: '',
    coverUrl: 'https://example.test/artist.jpg',
  }]);
  assert.equal(albums.length, 1);
  assert.equal(albums[0].type, 'album');
  assert.equal(albums[0].remoteId, 'ALBUM_MID');
  assert.equal(albums[0].artistRemoteId, 'ARTIST_MID');
  assert.deepEqual(playlists, [{
    type: 'playlist',
    remoteId: 'PLAYLIST_ID_AS_TEXT',
    title: '分类测试歌单',
    subtitle: '歌单创建者',
    coverUrl: 'https://example.test/playlist.jpg',
    popularity: 987654,
  }]);
});

test('normalizes hot terms and smartbox suggestions', () => {
  const fixture = JSON.parse(fs.readFileSync(
    path.join(__dirname, 'fixtures/search-discovery-response.json'), 'utf8',
  ));
  assert.deepEqual(contract.hotKeys(fixture), [
    { text: '热门关键词一', description: '热度说明', score: 987654 },
    { text: '热门关键词二', description: '', score: 123456 },
  ]);
  assert.deepEqual(contract.suggestions(fixture, 3), [
    {
      type: 'artist',
      remoteId: 'SUGGEST_ARTIST',
      text: '联想歌手',
      subtitle: '',
    },
    {
      type: 'song',
      remoteId: 'SUGGEST_SONG',
      text: '联想歌曲',
      subtitle: '联想歌手',
    },
    {
      type: 'album',
      remoteId: 'SUGGEST_ALBUM',
      text: '联想专辑',
      subtitle: '联想歌手',
    },
  ]);
});
