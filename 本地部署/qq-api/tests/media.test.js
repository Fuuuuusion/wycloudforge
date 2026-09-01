'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const contract = require('../contract');
const { mediaAddresses, remoteIds } = require('../media');

test('resolves media with songmid only even when file media id has a quality prefix', async () => {
  const calls = [];
  const sdk = {
    async getMusicPlay(options) {
      calls.push(options);
      return {
        status: 200,
        body: { data: { playUrl: {
          [options.songmid]: { url: 'https://example.test/vip-song.mp3' },
        } } },
      };
    },
  };

  const addresses = await mediaAddresses({
    items: [{
      remoteId: '0039MnYb0qxYhV',
      mediaRemoteId: 'C4000039MnYb0qxYhV',
    }],
    credential: 'qqmusic_uin=test-user; qqmusic_key=test-key',
  }, { sdk, contract });

  assert.deepEqual(calls, [{
    songmid: '0039MnYb0qxYhV',
    quality: '128',
    cookie: 'qqmusic_uin=test-user; qqmusic_key=test-key',
  }]);
  assert.equal(Object.hasOwn(calls[0], 'mediaId'), false);
  assert.deepEqual(addresses, [{
    remoteId: '0039MnYb0qxYhV',
    url: 'https://example.test/vip-song.mp3',
    error: '',
  }]);
});

test('keeps legacy ids and caps media batches without using media metadata', () => {
  const ids = Array.from({ length: 105 }, (_, index) => `SONG_${index}`);
  assert.deepEqual(remoteIds({ ids }).slice(0, 2), ['SONG_0', 'SONG_1']);
  assert.equal(remoteIds({ ids }).length, 100);
  assert.deepEqual(remoteIds({
    items: [{ remoteId: 'ITEM_SONG', mediaRemoteId: 'C400ITEM_SONG' }],
    ids: ['LEGACY_SONG'],
  }), ['ITEM_SONG']);
});
