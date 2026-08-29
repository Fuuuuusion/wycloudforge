'use strict';

const { parentPort, workerData } = require('node:worker_threads');
const sdk = require('@sansenjian/qq-music-api/sdk');

async function run() {
  const result = workerData.method === 'wechat'
    ? await sdk.checkWXLoginQr({ uuid: workerData.uuid })
    : await sdk.checkQQLoginQr({ ptqrtoken: workerData.ptqrtoken, qrsig: workerData.qrsig });
  parentPort.postMessage({ ok: true, result });
}

run().catch((error) => {
  parentPort.postMessage({ ok: false, error: error && error.message ? error.message : '登录状态查询失败' });
});
