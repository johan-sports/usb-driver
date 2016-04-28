var USBDriver = require('../src/usb-driver.js');

USBDriver.pollDevices()
  .then(function(usbDrives) {
    console.log("GetAll: "+usbDrives.length);
    console.log(usbDrives);

    usbDrives.forEach(function(usbDrive) {
      USBDriver.get(usbDrive.id)
        .then(function(usbDrive) {
          if (usbDrive) {
            console.log("Get: "+usbDrive.id);
            console.log(usbDrive);
            if (usbDrive.mount) {
              USBDriver.unmount(usbDrive.id)
                .then(function() {
                  console.log("Unmounted");
                })
                .catch(function() {
                  console.log("Not Unmounted");
                });
            }
          } else {
            console.log("Could not get "+usbDrives[i]);
          }
        });
    });
  });
