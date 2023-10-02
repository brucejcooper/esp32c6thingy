# CCPEED Home Automation devices.


We support a couple of different types of devices

* **DALI Bridge** - This exposes DALI lights as devices using the CCPEED device API
* **Switch** - This exposes up to 5 switches which can be configured to make changes to other devices. 
* **Relay** - This exposes up to 16 relays via GPIO

# Build variants
We support multip device types.  This is handled with sdkconfig files, one for each build variant. Use the `esp-idf` standard envrionment variables to specify and build into a different build directory.

For example, to build the dali-brdige variant, run
```sh
IDF_TARGET=esp32c6 SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.dalibridge' IDF_TARGET=esp32c6 idf.py -B build.dalibridge build
```
or the button variant
```sh
IDF_TARGET=esp32c6 SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.button' idf.py -B build.button -p /dev/ttyUSB0 build
```

Then when flashing

```sh
idf.py -B build.dalibridge flash monitor
```



# Theory of operation
Everything is done over vanilla COAP.  This makes it easy to understand, and easy to replicate and interact with

## Device binding
Often two decives will interact.  For example, a button might actuate a light switch, or an air conditioning unit might rely upon a temperature sensor to determine when to switch on.  There are two types of interaction that we need to undersrtand

1. Reacting to changes in other devices (e.g. aircon reacting to temperature)
1. Driving changes - e.g. light switch

In reality, both models can be represented on either end. The temperature sensor could drive changes to the aircon unit. Likewise, the light switch could subscribe to changes in the switch and act upon it. Really, the distinction comes down to what will happen if the two devices become disconnected.  For the airconditioner case, we want the airconditioner controller to be in charge of making decisions.  If the network were to go away, the controller should shut down and report an error. If it were the other way around, the airconditioner would remain stuck in its previous state indefinitely (or at least until a timeout occurred). Either way it would require additional configuration. 

In the light example, it is the person interacting with the switch who is "in charge".  They are telling the light to turn on or off.  To do it the other way round requires more communication to ensure that the link is in place at all times so that it can listen for events.  Likewise, we'd send more events than we need to, as the light switch would need to send _every_ event, whereas the light might only be interested in a handful of those events.   Subscribing to an event stream, especially when events may happen in quick succession, will require additional protocol overhead.  Better to handle that directly on the source, then turn that into direct actions.

Listening to attribute changes is easy - The listener sends a GET request to the source, with the Observable option set.  The source will persistently save that connection ID, and will send out a message each time that the attribute changes.  Given that a bunch of different attributes may be returned from a given aspect URL, the caller can include a filter option (using query options) to narrow down which changes should trigger a message.  All updates will be confirmable. 

Actions are configred on the action taking the action (e.g. the light switch).  The exact configuration of the conditions that cause an action to be taken are provider specific.  The action itself will be consistent however.  Each action will consist of the following attributes, stored as a CBOR array.

1. The Target device ID (not the IP address of the device)
2. The target aspect ID
3. The Attribute or Service ID that will be updated/called.  Attributes are negative, services are positive
4. In the case of an attribute update, the value will follow.  For service calls all remaining values in the array will be passed as arguments to the service call.

As an example, a light switch might have an action that is taken on click, at which point it will toggle the on/off state of a light fitting.  Its action would look like

```
[/*devid*/[/*type GTIN*/0x01, 12345, /* type SN */ 0x0123456789abcdef], /*aspect ON/OFF*/ 1, /* service TOGGLE*/ 3 /* no arguments*/]
```

each "event" may have multiple actions, and they should be stored as a CBOR array of the above type.



# Sample Device IDS
These are the devices that I tested with:

1. Relay     - GTIN 9006210757131, SERIAL: 0000000000.b50f08
1. LED Light - GTIN 8720053680265, SERIAL: 38581a0000.690292
