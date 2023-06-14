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
	static const uint32_t NUM_CHANNELS = 8;
	static const uint32_t LEDS_PER_CHANNEL = 64;
	static const uint32_t COLOURS_PER_LED = 3;
	static const uint32_t CHANNEL_DATA_SIZE = (LEDS_PER_CHANNEL * COLOURS_PER_LED);
	static const uint32_t FRAMEBUFFER_SIZE = (NUM_CHANNELS * CHANNEL_DATA_SIZE);

	static void transferThreadLoop(void* arg);

	void opcSetPixelColors(const OPC::Message &msg);
	void writeFramebuffer();

	unsigned char mLedData[FRAMEBUFFER_SIZE];
	unsigned char mBackBuffer[FRAMEBUFFER_SIZE];
	
	std::string mName;
	std::string mComPort;
	tthread::thread* mTransferThread;
    bool mFrameWaitingForSubmit;
	volatile bool mNewFrameReadyToSend;
	volatile bool mFrameTransferInProgress;
	volatile bool mKeepThreadRunning;
};
