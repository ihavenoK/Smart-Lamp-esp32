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
    colorNames: BLE.COLOR_NAMES,

    /* alarm */
    alarmModalOpen: false,
    alarmHour: 0,
    alarmMin: 0,
    alarmSec: 0,
    alarmLabel: 'Not set',
    hourRange: ['0','1','2','3','4','5','6','7','8','9','10','11','12','13','14','15','16','17','18','19','20','21','22','23'],
    minRange: ['0','1','2','3','4','5','6','7','8','9','10','11','12','13','14','15','16','17','18','19','20','21','22','23','24','25','26','27','28','29','30','31','32','33','34','35','36','37','38','39','40','41','42','43','44','45','46','47','48','49','50','51','52','53','54','55','56','57','58','59'],
    secRange: ['0','1','2','3','4','5','6','7','8','9','10','11','12','13','14','15','16','17','18','19','20','21','22','23','24','25','26','27','28','29','30','31','32','33','34','35','36','37','38','39','40','41','42','43','44','45','46','47','48','49','50','51','52','53','54','55','56','57','58','59']
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

  /* reconnect: 优先快速重连上次设备, 失败再回扫描页 */
  onReconnect: function () {
    var that = this;
    wx.showLoading({ title: 'reconnecting...', mask: true });

    BLE.reconnect()
      .then(function () {
        wx.hideLoading();
        app.globalData.connected = true;
        that.setData({ connected: true });
      })
      .catch(function () {
        wx.hideLoading();
        wx.redirectTo({ url: '/pages/connect/connect' });
      });
  },

  /* disconnect */
  onDisconnect: function () {
    BLE.disconnect();
    this.setData({ connected: false });
  },

  /* ===== alarm ===== */

  /* open alarm modal */
  onAlarmOpen: function () {
    this.setData({ alarmModalOpen: true });
  },

  /* close modal (no-op for catchtap) */
  noop: function () {},

  /* close modal */
  onAlarmModalClose: function () {
    this.setData({ alarmModalOpen: false });
  },

  /* pickers */
  onHourChange: function (e) {
    this.setData({ alarmHour: Number(e.detail.value) });
  },
  onMinChange: function (e) {
    this.setData({ alarmMin: Number(e.detail.value) });
  },
  onSecChange: function (e) {
    this.setData({ alarmSec: Number(e.detail.value) });
  },

  /* confirm: send [0x06][hour][min][sec] */
  onAlarmConfirm: function () {
    var that = this;
    var h = this.data.alarmHour;
    var m = this.data.alarmMin;
    var s = this.data.alarmSec;

    BLE.sendAlarmCommand(h, m, s)
      .then(function () {
        var label = (h < 10 ? '0' + h : h) + ':' +
                    (m < 10 ? '0' + m : m) + ':' +
                    (s < 10 ? '0' + s : s);
        that.setData({ alarmModalOpen: false, alarmLabel: label });
        wx.showToast({ title: 'Alarm set ' + label, icon: 'none' });
      })
      .catch(function () {
        wx.showToast({ title: 'send fail', icon: 'none' });
      });
  },

  /* cancel alarm: send [0x06][0][0][0] */
  onAlarmClear: function () {
    var that = this;
    BLE.sendAlarmCommand(0, 0, 0)
      .then(function () {
        that.setData({ alarmLabel: 'Not set' });
        wx.showToast({ title: 'Alarm cancelled', icon: 'none' });
      })
      .catch(function () {
        wx.showToast({ title: 'send fail', icon: 'none' });
      });
  }
});
