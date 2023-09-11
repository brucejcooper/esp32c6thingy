# CCPEED Home Automation devices.


We support a couple of different types of devices

* **DALI Bridge** - This exposes DALI lights as devices using the CCPEED device API
* **Switch** - This exposes up to 5 switches which can be configured to make changes to other devices. 
* **Relay** - This exposes up to 16 relays via GPIO

# Build variants
We support multip device types.  This is handled with sdkconfig files, one for each build variant. Use the `esp-idf` standard envrionment variables to specify and build into a different build directory.

For example, to build the dali-brdige variant, run
```sh
SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.dalibridge'  idf.py -B build.dalibridge build
```

Then when flashing

```sh
idf.py -B build.dalibridge flash monitor
```