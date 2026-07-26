// pages/connect/connect.js — BLE scan + connect (ES5)

var BLE = require('../../utils/ble');
var app = getApp();

Page({
  data: {
    scanning: false,
    devices: [],
    error: ''
  },

  onLoad: function () {
    var that = this;
    BLE.init()
      .then(function () { console.log('[connect] ble ok'); })
      .catch(function (e) { that.setData({ error: e }); });
  },

  onScanTap: function () {
    var that = this;
    that.setData({ scanning: true, devices: [], error: '' });

    BLE.startScan()
      .then(function (devices) {
        devices.sort(function (a, b) { return b.RSSI - a.RSSI; });
        that.setData({ scanning: false, devices: devices });
        if (devices.length === 0) {
          that.setData({ error: 'no SmartLamp found, check power' });
        }
      })
      .catch(function (e) {
        that.setData({ scanning: false, error: e });
      });
  },

  onDeviceTap: function (e) {
    var that = this;
    var deviceId = e.currentTarget.dataset.id;

    /* mark connecting */
    var devices = this.data.devices.map(function (d) {
      return {
        deviceId: d.deviceId,
        name: d.name,
        RSSI: d.RSSI,
        connecting: d.deviceId === deviceId
      };
    });
    this.setData({ devices: devices, error: '' });

    wx.showLoading({ title: 'connecting...', mask: true });

    BLE.connect(deviceId)
      .then(function () {
        wx.hideLoading();
        app.globalData.deviceId = deviceId;
        app.globalData.connected = true;
        wx.redirectTo({ url: '/pages/index/index' });
      })
      .catch(function (e) {
        wx.hideLoading();
        that.setData({
          error: 'conn fail: ' + (typeof e === 'string' ? e : JSON.stringify(e)),
          devices: that.data.devices.map(function (d) {
            return {
              deviceId: d.deviceId,
              name: d.name,
              RSSI: d.RSSI,
              connecting: false
            };
          })
        });
      });
  }
});
