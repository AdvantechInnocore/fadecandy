#pragma once

#include <cstring>
#include <set>
#include <vector>

#include "usbdevice.h"
#include "tinythread.h"

class AIDevice : public USBDevice
{
public:
	AIDevice(libusb_device *device, bool verbose);
	virtual ~AIDevice();

	static bool probe(libusb_device *device, const Value& devices_cfg, bool verbose);

	virtual int open() override;
	virtual bool probeAfterOpening() override;
	virtual bool matchConfiguration(const Value &config) override;
	virtual void loadConfiguration(const Value &config) override;
	virtual void writeMessage(const OPC::Message &msg) override;
	virtual void writeMessage(Document &msg) override;
	virtual void writeColorCorrection(const Value &color) override;
	virtual void flush() override;
	virtual void describe(rapidjson::Value &object, Allocator &alloc) override;
	virtual std::string getName() override;

private:
	static void transferThreadLoop(void* arg);

	void opcSetPixelColors(const OPC::Message &msg);
	void writeFramebuffer();

	uint32_t mLedDataSize = 0;
	uint8_t* mLedData = nullptr;
	uint8_t* mBackBuffer = nullptr;
	
	std::string mName;
	std::string mComPort;
	tthread::thread* mTransferThread;
    bool mFrameWaitingForSubmit;
	volatile bool mNewFrameReadyToSend;
	volatile bool mFrameTransferInProgress;
	volatile bool mKeepThreadRunning;
};
