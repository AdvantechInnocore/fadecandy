#include "aidevice.h"

#include <iostream>

#include <libusbi.h>

#include <vca_api.h>

#define IGL_GET_VERSION_MAJOR(ver)	 (((ver) & 0xf0000000) >> 28)
#define IGL_GET_VERSION_MINOR(ver)	 (((ver) & 0x0ff00000) >> 20)
#define IGL_GET_VERSION_MICRO(ver)	 (((ver) & 0x000ff000) >> 12)
#define IGL_GET_VERSION_BUILD(ver)	 (((ver) & 0x00000fff))

namespace
{
	std::string last_com_port;
	int last_version_no = 0;

	std::string formulateName(const std::string& com_port, bool verbose)
	{
		static const size_t BUFF_SIZE = 256;
		char name_buffer[BUFF_SIZE] = {0};
		size_t board_name_length = 31;
		unsigned char board_name[board_name_length + 1] = {0};
		unsigned int unique_id[3];
		unsigned int id_len = sizeof(unique_id);
		int param_no = 0;
		int ret = -1;

		param_no = vca_param_name_to_code("board");
		if (param_no <= 0)
		{
			if (verbose)
				std::clog << "Failed to get board name param number (" << param_no << ").\n";
			goto done;
		}
		ret = vca_get_parameter(NULL, param_no, 0, board_name, &board_name_length);
		if (VCA_SUCCESS != ret)
		{
			if (verbose)
				std::clog << "Failed to get board name (" << ret << ").\n";
			goto done;
		}

		param_no = vca_param_name_to_code("uniqid");
		if (param_no <= 0)
		{
			if (verbose)
				std::clog << "Failed to get unique id param number (" << param_no << ").\n";
			goto done;
		}
		ret = vca_get_parameter(NULL, param_no, 0, reinterpret_cast<unsigned char*>(unique_id), &id_len);
		if (VCA_SUCCESS != ret)
		{
			if (verbose)
				std::clog << "Failed to get board name (" << ret << ").\n";
			goto done;
		}

		sprintf(name_buffer, "Advantech LED Controller %s on %s (ID: %08x%08x%08x FW v%d.%d.%d.%d)", board_name, com_port.c_str(), unique_id[0], unique_id[1], unique_id[2],
			IGL_GET_VERSION_MAJOR(last_version_no),
			IGL_GET_VERSION_MINOR(last_version_no),
			IGL_GET_VERSION_MICRO(last_version_no),
			IGL_GET_VERSION_BUILD(last_version_no));

done:
		return name_buffer;
	}

	bool tryConnectToPort(const char* com_port, bool verbose)
	{
		if (verbose)
			std::clog << "Attempting to connect to " << com_port << " to see if there is an LED Controller.\n";
		int ret = vca_connect(com_port);
		if (verbose)
		{
			std::clog << "vca_connect result: " << ret;
			if (ret == VCA_SUCCESS)
				std::clog << " - success.\n";
			else
				std::clog << " - " << vca_last_os_error_string();
		}

		if (ret == VCA_SUCCESS)
		{
			ret = vca_get_firmware_version(NULL, &last_version_no, NULL);
			if (VCA_SUCCESS != ret)
			{
				if (verbose)
					std::clog << "Failed to get LED Controller firmware string (" << ret << ").\n";
				vca_disconnect();
				return false;
			}

			if (IGL_GET_VERSION_MAJOR(last_version_no) < 1 || IGL_GET_VERSION_MINOR(last_version_no) < 4)
			{
				std::clog << "LED Controller firmware (v"
					<< IGL_GET_VERSION_MAJOR(last_version_no) << "." << IGL_GET_VERSION_MINOR(last_version_no) << "." << IGL_GET_VERSION_MICRO(last_version_no) << "." << IGL_GET_VERSION_BUILD(last_version_no)
					<< ") is too old to work with Fadecandy.  Expected v1.4 or above.\n";
				vca_disconnect();
				return false;
			}

			last_com_port = com_port;
			return true;
		}
		return false;
	}

	bool tryConnectOnPortsInConfig(const USBDevice::Value& config, bool verbose)
	{
		const USBDevice::Value& ports = config["ailedc_comports"];
		if (ports.IsNull())
		{
			if (verbose)
				std::clog << "No 'ailedc_comports' property found in config file.\n";
			return false;
		}
		if (!ports.IsArray())
		{
			std::clog << "Expected 'ailedc_comports' to be an array.\n";
			return false;
		}

		for (int i = 0; i < ports.Size(); ++i)
		{
			const USBDevice::Value& port = ports[i];
			if (!port.IsString())
			{
				std::clog << "Expected values in 'ailedc_comports' to be strings.\n";
				return false;
			}

			if (tryConnectToPort(port.GetString(), verbose))
				return true;
		}

		return false;
	}

	void tryConnectOnAllports(bool verbose)
	{
		char com_port[32];
		if (verbose)
			std::clog << "Scanning all COM ports to find an LED Controller (this may be slow, better to add one to a config file).\n";
		for (int p = 1; p < 256; ++p)
		{
			sprintf(com_port, "COM%d", p);
			if (tryConnectToPort(com_port, verbose))
				return;
		}

		if (verbose)
			std::clog << "No LED Controller found on the COM ports.\n";
	}
}


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
	if (mVerbose)
		std::clog << "AIDevice destructor called.\n";

	if (mTransferThread)
	{
		if (mVerbose)
			std::clog << "Killing data transfer thread.\n";
		mKeepThreadRunning = false;
		mTransferThread->join();
		delete mTransferThread;
		if (mVerbose)
			std::clog << "Thread killed.\n";
	}

	vca_disconnect();
}

bool AIDevice::probe(libusb_device *device, const Value& config, bool verbose)
{
	libusb_device_descriptor dd;

	if (libusb_get_device_descriptor(device, &dd) < 0)
	{
		if (verbose)
			std::clog << "Failed to get libusb device descriptor.\n";
		return false; // Can't access descriptor?
	}

	if (dd.bDeviceClass != LIBUSB_CLASS_COMM)
	{
		return false; // Not a COM port device, ie not an Advantech Innocore LED Controller
	}

	char *connected_port = nullptr;
	if (vca_is_connected(&connected_port))
	{
		if (verbose)
			std::clog << "Already connected to an LED Controller.\n";
		return false; // Already connected, only one connection is currently supported
	}

	if (!tryConnectOnPortsInConfig(config, verbose))
		tryConnectOnAllports(verbose);
	
	if (last_com_port.length() == 0)
		return false;	// last_com_port will be configured if connection was successful
	
	return true;
}

int AIDevice::open()
{
	mComPort = last_com_port;
	last_com_port = "";
	mKeepThreadRunning = true;
	mTransferThread = new tthread::thread(transferThreadLoop, this);
	mName = formulateName(mComPort, mVerbose);
	return 0; // Already open
}

bool AIDevice::probeAfterOpening()
{
	if (mVerbose)
		std::clog << "probeAfterOpening() called.\n";
	return USBDevice::probeAfterOpening();
}
	
bool AIDevice::matchConfiguration(const Value &config)
{
	if (mVerbose)
		std::clog << "matchConfiguration() called.\n";
	return USBDevice::matchConfiguration(config);
}

void AIDevice::loadConfiguration(const Value &config)
{
	if (mVerbose)
		std::clog << "loadConfiguration() called.\n";
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

void AIDevice::writeMessage(Document &msg)
{
	if (mVerbose)
		std::clog << "writeMessage(Document) called.\n";
	USBDevice::writeMessage(msg);
}
	
void AIDevice::writeColorCorrection(const Value &color)
{
	if (mVerbose)
		std::clog << "writeColorCorrection() called.\n";
	USBDevice::writeColorCorrection(color);
}

void AIDevice::flush()
{
	// Submit new frames, if we had a queued frame waiting
	if (mFrameWaitingForSubmit && !mFrameTransferInProgress)
	{
		writeFramebuffer();
	}
}

void AIDevice::describe(rapidjson::Value &object, Allocator &alloc)
{
	if (mVerbose)
		std::clog << "describe() called.\n";
	USBDevice::describe(object, alloc);
}
	
std::string AIDevice::getName()
{
	return mName;
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
