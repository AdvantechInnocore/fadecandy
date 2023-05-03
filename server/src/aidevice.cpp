#include "aidevice.h"

#include <iostream>

#include <libusbi.h>

#include <vca_api.h>


AIDevice::AIDevice(libusb_device *device, bool verbose) : USBDevice(device, "Advantech Innocore LED Controller", verbose),
														  mTransferThread(nullptr),
														  mFrameWaitingForSubmit(false),
														  mNewFrameReadyToSend(false),
														  mFrameTransferInProgress(false),
														  mKeepThreadRunning(false)
{
}

AIDevice::~AIDevice()
{
	vca_disconnect();

	if (mTransferThread)
	{
		mKeepThreadRunning = false;
		mTransferThread->join();
		delete mTransferThread;
	}
}

bool AIDevice::probe(libusb_device *device, bool verbose)
{
	libusb_device_descriptor dd;

	if (libusb_get_device_descriptor(device, &dd) < 0)
	{
		return false; // Can't access descriptor?
	}

	if (dd.bDeviceClass != LIBUSB_CLASS_COMM)
	{
		return false; // Not a COM port device, ie not an Advantech Innocore LED Controller
	}

	char *connected_port = nullptr;
	if (vca_is_connected(&connected_port))
	{
		return false; // Already connected, only one connection is currently supported
	}

	char com_port[32];
	for (int p = 0; p < 256; ++p)
	{
		sprintf(com_port, "COM%d", p);
		int ret = vca_connect(com_port);
		if (verbose)
		{
			std::clog << "vca_connect result for COM" << p << ": " << ret;
			if (ret == VCA_SUCCESS)
				std::clog << " - success.\n";
			else
				std::clog << " - " << vca_last_os_error_string();
		}

		if (ret == VCA_SUCCESS)
		{
			return true;
		}
	}

	return false;
}

int AIDevice::open()
{
	mKeepThreadRunning = true;
	mTransferThread = new tthread::thread(transferThreadLoop, this);
	return 0; // Already open
}

void AIDevice::loadConfiguration(const Value &config)
{
	if (mVerbose)
	{
		std::clog << "MICK: implement loadConfiguration()\n";
	}
}

void AIDevice::writeMessage(const OPC::Message &msg)
{
	switch (msg.command)
	{
	case OPC::SetPixelColors:
	{
		opcSetPixelColors(msg);
		writeFramebuffer();
	}
	break;

	case OPC::SystemExclusive:
	{
		if (msg.length() < 4)
		{
			if (mVerbose)
			{
				std::clog << "SysEx message too short!\n";
			}
			return;
		}

		unsigned id = (unsigned(msg.data[0]) << 24) |
					  (unsigned(msg.data[1]) << 16) |
					  (unsigned(msg.data[2]) << 8) |
					  unsigned(msg.data[3]);
		if (mVerbose)
		{
			std::clog << "SysEx command: " << id << "\n";
		}
	}
	break;

	default:
	{
		if (mVerbose)
		{
			std::clog << "Unhandled OPC command: " << msg.command << "\n";
		}
	}
	break;
	}
}

std::string AIDevice::getName()
{
	if (mVerbose)
	{
		std::clog << "MICK: implement getName()\n";
	}
	return "Bob";
}

void AIDevice::flush()
{
	// Submit new frames, if we had a queued frame waiting
	if (mFrameWaitingForSubmit && !mFrameTransferInProgress)
	{
		writeFramebuffer();
	}
}

void AIDevice::transferThreadLoop(void *arg)
{
	AIDevice* self = static_cast<AIDevice*>(arg);
	while (self->mKeepThreadRunning)
	{
		while (!self->mNewFrameReadyToSend)
		{
			if (!self->mKeepThreadRunning)
				return;
		}
		self->mFrameTransferInProgress = true;
		self->mNewFrameReadyToSend = false;
		unsigned char* led_data = self->mLedData;
		for (int channel = 0; channel < NUM_CHANNELS; ++channel)
		{
			vca_send_raw_led_data(channel, 1, led_data, LEDS_PER_CHANNEL);
			led_data += CHANNEL_DATA_SIZE;
		}
		vca_refresh_led_strips();
		self->mFrameTransferInProgress = false;
	}
}

// Takes an incoming message and sets the framebuffer colours as appropriate
// For now, mapping is ignored, but will be implemented in the future
void AIDevice::opcSetPixelColors(const OPC::Message &msg)
{
	/*
	 * Parse through our device's mapping, and store any relevant portions of 'msg'
	 * in the framebuffer.
	 */

	//	if (!mConfigMap)
	//	{
	//		// No mapping defined yet. This device is inactive.
	//		return;
	//	}

	//	const Value &map = *mConfigMap;
	//	for (unsigned i = 0, e = map.Size(); i != e; i++)
	{
		opcMapPixelColors(msg /*, map[i]*/);
	}
}

// Performs the framebuffer update based on the mapping (mapping ignored for now)
void AIDevice::opcMapPixelColors(const OPC::Message &msg /*, const Value &inst*/)
{
	if (msg.length() > FRAMEBUFFER_SIZE)
	{
		if (mVerbose)
		{
			std::clog << "Framebuffer size (" << FRAMEBUFFER_SIZE << ") too small for incoming message size: " << msg.length() << "\n";
		}
		return;
	}
	memcpy(mBackBuffer, msg.data, msg.length());
}

void AIDevice::writeFramebuffer()
{
	/*
	 * Asynchronously write the current framebuffer.
	 *
	 * Currently if this gets ahead of what the USB device is capable of,
	 *       we always drop frames.
	 */

	if (mFrameTransferInProgress)
	{
		// Too many outstanding frames. Wait to submit until a previous frame completes.
		mFrameWaitingForSubmit = true;
		return;
	}

	memcpy(mLedData, mBackBuffer, FRAMEBUFFER_SIZE);
	mNewFrameReadyToSend = true;
}
