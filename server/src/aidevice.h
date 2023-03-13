#pragma once
#include "usbdevice.h"

class AIDevice : public USBDevice
{
public:
	AIDevice(libusb_device *device, bool verbose);
	virtual ~AIDevice();

	static bool probe(libusb_device *device);

	virtual int open() override;
	virtual void loadConfiguration(const Value &config) override;
	virtual void writeMessage(const OPC::Message &msg) override;
//	virtual void writeMessage(Document &msg);
//	virtual void writeColorCorrection(const Value &color);
	virtual std::string getName() override;
	virtual void flush() override;
//	virtual void describe(rapidjson::Value &object, Allocator &alloc);
};
