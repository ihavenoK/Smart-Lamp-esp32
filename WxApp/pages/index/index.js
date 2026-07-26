// pages/index/index.js — main control (ES5)

var BLE = require('../../utils/ble');
var app = getApp();

Page({
  data: {
    connected: false,

    state: {
      mode: 0,
      light: 0,
      color: 0,
      temp: 0,
      humi: 0,
      study_time: 0
    },

    modeNames: BLE.MODE_NAMES,
    modeEmojis: ['💡','❄️','🔥','🌈','🌙','📖','🤖'],
    colorNames: BLE.COLOR_NAMES
  },

  onLoad: function () {
    var that = this;

    BLE.onNotify(function (state) {
      that.setData({ state: state, connected: true });
    });

    BLE.onConnect(function () {
      app.globalData.connected = true;
      that.setData({ connected: true });
    });

    BLE.onDisconnect(function () {
      app.globalData.connected = false;
      that.setData({ connected: false });
    });

    if (app.globalData.connected) {
      this.setData({ connected: true });
    }
  },

  onShow: function () {
    this.setData({ connected: BLE.isConnected() });
  },

  /* mode */
  onModeTap: function (e) {
    var mode = e.currentTarget.dataset.mode;
    if (!BLE.isConnected()) {
      wx.showToast({ title: 'not connected', icon: 'none' });
      return;
    }

    var that = this;
    BLE.sendCommand(mode, this.data.state.light, this.data.state.color)
      .then(function () {
        that.setData({ 'state.mode': mode });
      })
      .catch(function (err) {
        wx.showToast({ title: 'send fail', icon: 'none' });
        console.error(err);
      });
  },

  /* brightness slider */
  onBrightnessChange: function (e) {
    var level = e.detail.value;
    if (!BLE.isConnected()) return;

    var that = this;
    BLE.sendCommand(this.data.state.mode, level, this.data.state.color)
      .then(function () {
        that.setData({ 'state.light': level });
      })
      .catch(function () {
        wx.showToast({ title: 'send fail', icon: 'none' });
      });
  },

  /* color */
  onColorTap: function (e) {
    var color = e.currentTarget.dataset.color;
    if (!BLE.isConnected()) return;

    var that = this;
    BLE.sendCommand(this.data.state.mode, this.data.state.light, color)
      .then(function () {
        that.setData({ 'state.color': color });
      })
      .catch(function () {
        wx.showToast({ title: 'send fail', icon: 'none' });
      });
  },

  /* reconnect */
  onReconnect: function () {
    wx.redirectTo({ url: '/pages/connect/connect' });
  },

  /* disconnect */
  onDisconnect: function () {
    BLE.disconnect();
    this.setData({ connected: false });
  }
});
