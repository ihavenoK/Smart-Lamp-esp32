/**
 * ble.js — SmartLamp BLE core (ES5)
 *
 * Protocol:
 *   Service:  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *   RX (Write): 6E400002-...  Phone→Lamp  3 bytes [mode][light][color]
 *   TX (Notify): 6E400003-...  Lamp→Phone  6 bytes [mode][light][color][temp][humi][study]
 */

var DEVICE_NAME = 'SmartLamp';

var SERVICE_UUID = '6E400001-B5A3-F393-E0A9-E50E24DCCA9E';
var RX_CHAR_UUID  = '6E400002-B5A3-F393-E0A9-E50E24DCCA9E';
var TX_CHAR_UUID  = '6E400003-B5A3-F393-E0A9-E50E24DCCA9E';

var MODE_NAMES = ['常规','冷光','暖光','氛围','夜灯','学习','自控'];
var COLOR_NAMES = ['白','青','黄','紫','蓝','红','绿'];

/* ——— internal state ——— */

var s_deviceId = '';
var s_connected = false;
var s_notifyReady = false;

/* ——— callbacks ——— */

var s_onNotify = null;
var s_onConnect = null;
var s_onDisconnect = null;

function onNotify(fn)  { s_onNotify = fn; }
function onConnect(fn) { s_onConnect = fn; }
function onDisconnect(fn) { s_onDisconnect = fn; }

function isConnected() { return s_connected; }

/* ===== init ===== */

function init() {
  return new Promise(function (resolve, reject) {
    wx.openBluetoothAdapter({
      success: function () {
        wx.onBluetoothAdapterStateChange(function (res) {
          if (!res.available && s_connected) {
            s_connected = false;
            if (s_onDisconnect) s_onDisconnect();
          }
        });

        wx.onBLEConnectionStateChange(function (res) {
          if (!res.connected && s_deviceId) {
            s_connected = false;
            s_notifyReady = false;
            s_deviceId = '';
            if (s_onDisconnect) s_onDisconnect();
          }
        });

        wx.onBLECharacteristicValueChange(function (res) {
          if (res.characteristicId.toUpperCase() === TX_CHAR_UUID) {
            var buf = res.value;
            if (buf.byteLength >= 6) {
              var data = new Uint8Array(buf);
              var state = {
                mode:       data[0],
                light:      data[1],
                color:      data[2],
                temp:       data[3],
                humi:       data[4],
                study_time: data[5]
              };
              if (s_onNotify) s_onNotify(state);
            }
          }
        });

        resolve();
      },
      fail: function (err) {
        reject('ble init fail: ' + JSON.stringify(err));
      }
    });
  });
}

/* ===== scan ===== */

function startScan() {
  return new Promise(function (resolve, reject) {
    wx.startBluetoothDevicesDiscovery({
      allowDuplicatesKey: false,
      interval: 0,
      success: function () {
        var devices = {};
        wx.onBluetoothDeviceFound(function (res) {
          for (var i = 0; i < res.devices.length; i++) {
            var d = res.devices[i];
            var name = d.name || d.localName || '';
            if (name === DEVICE_NAME || d.advertisServiceUUIDs) {
              devices[d.deviceId] = d;
            }
          }
        });

        setTimeout(function () {
          wx.stopBluetoothDevicesDiscovery();
          var list = [];
          var keys = Object.keys(devices);
          for (var i = 0; i < keys.length; i++) {
            var d = devices[keys[i]];
            list.push({
              deviceId: d.deviceId,
              name: d.name || d.localName || 'Unknown',
              RSSI: d.RSSI
            });
          }
          resolve(list);
        }, 5000);
      },
      fail: function (err) {
        reject('scan fail: ' + JSON.stringify(err));
      }
    });
  });
}

/* ===== connect ===== */

function connect(deviceId) {
  return new Promise(function (resolve, reject) {
    wx.stopBluetoothDevicesDiscovery();

    wx.createBLEConnection({
      deviceId: deviceId,
      success: function () {
        s_deviceId = deviceId;
        s_connected = true;

        wx.getBLEDeviceServices({
          deviceId: deviceId,
          success: function (svcRes) {
            var svc = null;
            for (var i = 0; i < svcRes.services.length; i++) {
              if (svcRes.services[i].uuid.toUpperCase() === SERVICE_UUID) {
                svc = svcRes.services[i];
                break;
              }
            }
            if (!svc) {
              s_connected = false;
              reject('NUS svc not found');
              return;
            }

            wx.getBLEDeviceCharacteristics({
              deviceId: deviceId,
              serviceId: svc.uuid,
              success: function (chrRes) {
                var rxChar = null;
                var txChar = null;
                for (var j = 0; j < chrRes.characteristics.length; j++) {
                  var uuid = chrRes.characteristics[j].uuid.toUpperCase();
                  if (uuid === RX_CHAR_UUID) rxChar = chrRes.characteristics[j];
                  if (uuid === TX_CHAR_UUID) txChar = chrRes.characteristics[j];
                }
                if (!rxChar || !txChar) {
                  s_connected = false;
                  reject('NUS chr not found');
                  return;
                }

                wx.setBLEMTU({
                  deviceId: deviceId,
                  mtu: 512,
                  complete: function () {
                    wx.notifyBLECharacteristicValueChange({
                      deviceId: deviceId,
                      serviceId: svc.uuid,
                      characteristicId: txChar.uuid,
                      state: true,
                      success: function () {
                        s_notifyReady = true;
                        if (s_onConnect) s_onConnect();
                        resolve({
                          deviceId: deviceId,
                          svcUuid: svc.uuid,
                          rxUuid: rxChar.uuid,
                          txUuid: txChar.uuid
                        });
                      },
                      fail: function (err) {
                        reject('notify fail: ' + JSON.stringify(err));
                      }
                    });
                  }
                });
              },
              fail: function (err) { reject('chr fail: ' + JSON.stringify(err)); }
            });
          },
          fail: function (err) { reject('svc fail: ' + JSON.stringify(err)); }
        });
      },
      fail: function (err) { reject('conn fail: ' + JSON.stringify(err)); }
    });
  });
}

/* ===== send ===== */

function sendCommand(mode, light, color) {
  return new Promise(function (resolve, reject) {
    if (!s_connected || !s_deviceId) {
      reject('not connected');
      return;
    }
    var buf = new ArrayBuffer(3);
    var data = new Uint8Array(buf);
    data[0] = mode;
    data[1] = light;
    data[2] = color;

    wx.writeBLECharacteristicValue({
      deviceId: s_deviceId,
      serviceId: SERVICE_UUID,
      characteristicId: RX_CHAR_UUID,
      value: buf,
      success: function () { resolve(); },
      fail: function (err) { reject('send fail: ' + JSON.stringify(err)); }
    });
  });
}

/* ===== disconnect ===== */

function disconnect() {
  if (s_deviceId) {
    wx.closeBLEConnection({ deviceId: s_deviceId });
    s_deviceId = '';
  }
  s_connected = false;
  s_notifyReady = false;
}

/* ===== export ===== */

module.exports = {
  DEVICE_NAME: DEVICE_NAME,
  MODE_NAMES: MODE_NAMES,
  COLOR_NAMES: COLOR_NAMES,
  init: init,
  startScan: startScan,
  connect: connect,
  sendCommand: sendCommand,
  disconnect: disconnect,
  isConnected: isConnected,
  onNotify: onNotify,
  onConnect: onConnect,
  onDisconnect: onDisconnect
};
