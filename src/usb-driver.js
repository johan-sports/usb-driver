var USBNativeDriver = require('../build/Release/usb_driver.node');

/*
Device Object
{
  id: '12:12:WHATEVER',
  vendorCode: '0x0a',
  productCode: '0x12',
  manufacturer: 'Foo Bar Technologies',
  product: 'Baz Sensing Quux',
  serialNumber: 'IDQFB0023AB',
  mount: '/Volumes/FOOBAR1'
}
*/

var usbDriver = function() {
  var self = {};

  self.pollDevices  = pollDevices;
  self.get          = get;
  self.unmount      = unmount;

  return self;

  function pollDevices() {
    return new Promise(function(resolve) {
      resolve(USBNativeDriver.pollDevices());
    });
  }

  function get(id) {
    return new Promise(function(resolve) {
      resolve(USBNativeDriver.getDevice(id));
    });
  }

  function unmount(id) {
    return new Promise(function(resolve, reject) {
      if(USBNativeDriver.unmount(id)) {
        resolve();
      } else {
        reject();
      }
    });
  }
};

//USBDriver.prototype.on = function(event, callback) {
  //DiskWatcher.on(event, callback);
//};

//USBDriver.prototype.waitForEvents = function() {
  //console.log("WARNING: This method will not return.");
  //USBNativeDriver.waitForEvents();
//};

module.exports = usbDriver;
