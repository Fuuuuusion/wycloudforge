'use strict';

const defaultSdk = require('@sansenjian/qq-music-api/sdk');
const defaultContract = require('./contract');

function remoteIds(body) {
  const legacyIds = Array.isArray(body.ids) ? body.ids : [];
  const itemIds = Array.isArray(body.items)
    ? body.items.map((item) => item && item.remoteId)
    : [];
  return (itemIds.length ? itemIds : legacyIds)
    .map((remoteId) => String(remoteId || '').trim())
    .filter(Boolean)
    .slice(0, 100);
}

async function mediaAddresses(body, dependencies = {}) {
  const musicSdk = dependencies.sdk || defaultSdk;
  const contract = dependencies.contract || defaultContract;
  const credential = String(body.credential || '');
  const data = [];
  for (const remoteId of remoteIds(body)) {
    try {
      // @sansenjian/qq-music-api builds the vkey filename from songmid itself.
      // QQ file.media_mid may contain a quality prefix such as C400; forwarding
      // it as mediaId corrupts a 128 kbps M500 filename and breaks VIP playback.
      const raw = contract.unwrap(await musicSdk.getMusicPlay({
        songmid: remoteId,
        quality: '128',
        cookie: credential,
      }));
      const address = contract.mediaAddress(raw, remoteId);
      if (!address.url && !address.error)
        address.error = 'QQ 音乐没有返回可用播放地址';
      data.push(address);
    } catch (error) {
      data.push({ remoteId, url: '', error: error.message || '获取播放地址失败' });
    }
  }
  return data;
}

module.exports = {
  mediaAddresses,
  remoteIds,
};
