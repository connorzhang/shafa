// pages/index/index.js
const app = getApp()

function ab2hex(buffer) {
  const hexArr = Array.prototype.map.call(
    new Uint8Array(buffer),
    function (bit) {
      return ('00' + bit.toString(16)).slice(-2)
    }
  )
  return hexArr.join(' ').toUpperCase()
}

function hex2ab(hex) {
  var typedArray = new Uint8Array(hex.match(/[\da-f]{2}/gi).map(function (h) {
    return parseInt(h, 16)
  }))
  return typedArray.buffer
}

function formatTime(date) {
  const hour = date.getHours()
  const minute = date.getMinutes()
  const second = date.getSeconds()
  const ms = date.getMilliseconds()
  return `${[hour, minute, second].map(n => n.toString().padStart(2, '0')).join(':')}.${ms.toString().padStart(3, '0')}`
}

Page({
  data: {
    statusText: '未初始化',
    isScanning: false,
    devices: [],
    connected: false,
    connectedDeviceId: '',
    connectedDeviceName: '',
    services: [],
    selectedServiceId: '',
    characteristics: [],
    selectedCharId: '',
    customHex: '',
    logs: [],
    scrollToId: '',
    logCount: 0
  },

  onLoad() {
    this.initBlue()
  },

  onUnload() {
    this.closeBlue()
  },

  addLog(msg, type = 'info') {
    const time = formatTime(new Date())
    const id = this.data.logCount++
    const log = { id, time, msg, type }
    const logs = this.data.logs
    logs.push(log)
    if (logs.length > 100) logs.shift() // 保留最近100条
    
    this.setData({
      logs,
      scrollToId: `log-${id}`
    })
    console.log(`[${type}] ${msg}`)
  },

  initBlue() {
    this.addLog('初始化蓝牙...')
    wx.openBluetoothAdapter({
      success: (res) => {
        this.setData({ statusText: '蓝牙就绪' })
        this.addLog('蓝牙初始化成功')
        
        // 监听寻找新设备
        wx.onBluetoothDeviceFound((res) => {
          res.devices.forEach(device => {
            if (!device.name && !device.localName) return
            
            const devices = this.data.devices
            const idx = devices.findIndex(item => item.deviceId === device.deviceId)
            if (idx === -1) {
              devices.push(device)
            } else {
              devices[idx] = device
            }
            this.setData({ devices })
          })
        })
      },
      fail: (err) => {
        this.setData({ statusText: '蓝牙初始化失败' })
        this.addLog(`蓝牙初始化失败: ${err.errMsg}`, 'err')
        if (err.errCode === 10001) {
          wx.showModal({
            title: '提示',
            content: '请检查手机蓝牙是否打开',
            showCancel: false
          })
        }
      }
    })
  },

  startScan() {
    if (this.data.isScanning) {
      wx.stopBluetoothDevicesDiscovery()
      this.setData({ isScanning: false })
      this.addLog('停止扫描')
      return
    }

    this.setData({ devices: [], isScanning: true })
    this.addLog('开始扫描外围设备...')
    wx.startBluetoothDevicesDiscovery({
      allowDuplicatesKey: false,
      success: (res) => {
        this.addLog('扫描指令下发成功')
      },
      fail: (err) => {
        this.setData({ isScanning: false })
        this.addLog(`扫描失败: ${err.errMsg}`, 'err')
      }
    })
  },

  connectDevice(e) {
    const deviceId = e.currentTarget.dataset.id
    const name = e.currentTarget.dataset.name
    
    wx.stopBluetoothDevicesDiscovery()
    this.setData({ isScanning: false })
    
    this.addLog(`正在连接设备: ${name}...`)
    wx.showLoading({ title: '连接中' })
    
    wx.createBLEConnection({
      deviceId,
      success: (res) => {
        this.setData({
          connected: true,
          connectedDeviceId: deviceId,
          connectedDeviceName: name,
          statusText: '已连接'
        })
        this.addLog(`连接成功!`)
        wx.hideLoading()
        this.getServices(deviceId)
        
        // 监听连接断开
        wx.onBLEConnectionStateChange((res) => {
          if (!res.connected) {
            this.addLog('设备已断开连接', 'err')
            this.setData({
              connected: false,
              connectedDeviceId: '',
              services: [],
              characteristics: [],
              selectedServiceId: '',
              selectedCharId: '',
              statusText: '蓝牙就绪'
            })
          }
        })
      },
      fail: (err) => {
        this.addLog(`连接失败: ${err.errMsg}`, 'err')
        wx.hideLoading()
        wx.showToast({ title: '连接失败', icon: 'none' })
      }
    })
  },

  disconnectDevice() {
    if (!this.data.connectedDeviceId) return
    wx.closeBLEConnection({
      deviceId: this.data.connectedDeviceId,
      success: () => {
        this.addLog('已主动断开连接')
      }
    })
  },

  getServices(deviceId) {
    this.addLog('获取服务列表...')
    wx.getBLEDeviceServices({
      deviceId,
      success: (res) => {
        this.addLog(`找到 ${res.services.length} 个服务`)
        this.setData({ services: res.services })
        
        // 如果只有一个服务，或者有某个特定的主服务，可以自动选择
        // 这里为了测试方便，让用户手动点击选择
      },
      fail: (err) => {
        this.addLog(`获取服务失败: ${err.errMsg}`, 'err')
      }
    })
  },

  selectService(e) {
    const uuid = e.currentTarget.dataset.uuid
    this.setData({ selectedServiceId: uuid })
    this.addLog(`已选择服务: ${uuid}`)
    this.getCharacteristics(this.data.connectedDeviceId, uuid)
  },

  getCharacteristics(deviceId, serviceId) {
    this.addLog('获取特征值列表...')
    wx.getBLEDeviceCharacteristics({
      deviceId,
      serviceId,
      success: (res) => {
        this.addLog(`找到 ${res.characteristics.length} 个特征值`)
        this.setData({ characteristics: res.characteristics })
        
        // 自动订阅所有支持 notify/indicate 的特征值
        res.characteristics.forEach(item => {
          if (item.properties.notify || item.properties.indicate) {
            this.addLog(`订阅特征值: ${item.uuid}`)
            wx.notifyBLECharacteristicValueChange({
              state: true,
              deviceId,
              serviceId,
              characteristicId: item.uuid,
              success: () => {
                this.addLog('订阅成功')
                this.listenValueChange()
              },
              fail: (err) => {
                this.addLog(`订阅失败: ${err.errMsg}`, 'err')
              }
            })
          }
        })
      },
      fail: (err) => {
        this.addLog(`获取特征值失败: ${err.errMsg}`, 'err')
      }
    })
  },

  selectChar(e) {
    const uuid = e.currentTarget.dataset.uuid
    const canWrite = e.currentTarget.dataset.write
    if (!canWrite) {
      wx.showToast({ title: '该特征值不支持写入', icon: 'none' })
      return
    }
    this.setData({ selectedCharId: uuid })
    this.addLog(`已选择写入特征值: ${uuid}`)
  },

  listenValueChange() {
    wx.onBLECharacteristicValueChange((res) => {
      const hex = ab2hex(res.value)
      this.addLog(`收到数据: ${hex}`, 'rx')
    })
  },

  onHexInput(e) {
    this.setData({ customHex: e.detail.value })
  },

  sendCustomData() {
    let hex = this.data.customHex.replace(/\s+/g, '') // 移除空格
    if (hex.length % 2 !== 0) {
      wx.showToast({ title: 'Hex长度必须是偶数', icon: 'none' })
      return
    }
    this.sendHexData(hex)
  },

  sendTestData(e) {
    const hex = e.currentTarget.dataset.hex
    this.sendHexData(hex)
  },

  sendHexData(hexStr) {
    if (!this.data.connectedDeviceId || !this.data.selectedCharId) {
      wx.showToast({ title: '未准备好', icon: 'none' })
      return
    }

    const buffer = hex2ab(hexStr)
    
    this.addLog(`发送数据: ${ab2hex(buffer)}`, 'tx')
    
    wx.writeBLECharacteristicValue({
      deviceId: this.data.connectedDeviceId,
      serviceId: this.data.selectedServiceId,
      characteristicId: this.data.selectedCharId,
      value: buffer,
      success: () => {
        this.addLog('写入成功')
      },
      fail: (err) => {
        this.addLog(`写入失败: ${err.errMsg}`, 'err')
      }
    })
  },

  clearLogs() {
    this.setData({ logs: [], logCount: 0 })
  },

  closeBlue() {
    wx.closeBluetoothAdapter()
  }
})
