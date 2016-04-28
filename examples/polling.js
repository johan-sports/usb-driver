const usbDriver = require('../src/usb-driver.js');

// Poll time in milliseconds
const POLL_TIME = 1500;

function logDevices() {
  usbDriver.pollDevices()
    .then(function(usbDrives) {
      console.log('Number Polled: ' + usbDrives.length);
      console.log(usbDrives);
    })
    .catch(function(error) {
      console.error('Failed to poll devices:\n\t' + error);
    });

  setTimeout(logDevices, POLL_TIME);
}

// Run
logDevices();
