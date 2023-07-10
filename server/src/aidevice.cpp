#include "aidevice.h"

#include <iostream>

#include <libusbi.h>

#include <vca_api.h>

#define IGL_GET_VERSION_MAJOR(ver)	 (((ver) & 0xf0000000) >> 28)
#define IGL_GET_VERSION_MINOR(ver)	 (((ver) & 0x0ff00000) >> 20)
#define IGL_GET_VERSION_MICRO(ver)	 (((ver) & 0x000ff000) >> 12)
#define IGL_GET_VERSION_BUILD(ver)	 (((ver) & 0x00000fff))

#define COLOURS_PER_LED 3
#define ADVANTECH_VENDOR_ID 0x1809
#define LED_CONTROLLER_PRODUCT_ID 0x0203

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

			if (VCA_SUCCESS != (ret = vca_set_pattern_playback_state(VCA_PATTERN_PLAYBACK_PAUSED)))
			{
				if (verbose)
					std::clog << "Failed to stop the LED Controller running patterns (" << ret << ").\n";
				vca_disconnect();
				return false;
			}

			if (VCA_SUCCESS != (ret = vca_reset_all_layers()))
			{
				if (verbose)
					std::clog << "Failed to get reset all LED Controller layers (" << ret << ").\n";
				vca_disconnect();
				return false;
			}

			if (VCA_SUCCESS != (ret = vca_set_all_channels_brightness(31)))
			{
				if (verbose)
					std::clog << "Failed to set all LED Controller channel brightness settings (" << ret << ").\n";
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
			if (tryConnectToPort(com_port, false))
				return;
		}

		if (verbose)
			std::clog << "No LED Controller found on the COM ports.\n";
	}

	bool tryGetUnsignedValue(const USBDevice::Value& protocol_config, const char* param_name, bool verbose, unsigned int& out_val)
	{
		const USBDevice::Value& val = protocol_config[param_name];
		if (val.IsNull())
		{
			return false;
		}

		if (!val.IsUint())
		{
			if (verbose)
				std::clog << "Expected 'protocol." << param_name << "' property in config file to be an unsigned integer.\n";
			return false;
		}

		out_val = val.GetUint();

		return true;
	}
		
	void configureLedProtocol(const USBDevice::Value& protocol_config, bool verbose)
	{
		int vca_ret = 0;
		unsigned int val = 0;
		bool custom = false;

		if (!protocol_config.IsObject())
		{
			if (verbose)
				std::clog << "Expected 'protocol' property in config file to be an object.\n";
			return;
		}

		vca_led_protocol_settings_t protocol_settings;
		const USBDevice::Value& base = protocol_config["base"];
		if (!base.IsNull())
		{
			if (!base.IsString())
			{
				if (verbose)
					std::clog << "Expected 'protocol.base' property in config file to be aa string.\n";
				return;
			}

			vca_ret = vca_set_led_protocol_from_preset_name((char*)(base.GetString()));
			if (vca_ret != VCA_SUCCESS)
			{
				if (verbose)
					std::clog << "Failed to get the current LED protocol settings.\n";
				return;
			}
		}

		vca_ret = vca_get_led_protocol(&protocol_settings, sizeof(vca_led_protocol_settings_t));
		if (vca_ret != VCA_SUCCESS)
		{
			if (verbose)
				std::clog << "Failed to get the current LED protocol settings.\n";
			return;
		}

		if (tryGetUnsignedValue(protocol_config, "max_leds", verbose, val))
		{
			protocol_settings.max_leds = val;
			custom = true;
		}

		if (tryGetUnsignedValue(protocol_config, "protocol_type", verbose, val))
		{
			protocol_settings.protocol_type = val;
			custom = true;
		}

		if (tryGetUnsignedValue(protocol_config, "data_size", verbose, val))
		{
			protocol_settings.data_size = val;
			custom = true;
		}

		if (tryGetUnsignedValue(protocol_config, "bit_rate_khz", verbose, val))
		{
			protocol_settings.bit_rate_khz = val;
			custom = true;
		}

		if (tryGetUnsignedValue(protocol_config, "waveform_algorithm", verbose, val))
		{
			protocol_settings.waveform_algorithm = val;
			custom = true;
		}

		if (tryGetUnsignedValue(protocol_config, "colour_order", verbose, val))
		{
			protocol_settings.colour_order = val;
			custom = true;
		}

		if (tryGetUnsignedValue(protocol_config, "init_marker_type", verbose, val))
		{
			protocol_settings.init_marker_type = val;
			custom = true;
		}

		if (tryGetUnsignedValue(protocol_config, "init_marker_size", verbose, val))
		{
			protocol_settings.init_marker_size = val;
			custom = true;
		}

		if (tryGetUnsignedValue(protocol_config, "termination_marker_type", verbose, val))
		{
			protocol_settings.termination_marker_type = val;
			custom = true;
		}

		if (tryGetUnsignedValue(protocol_config, "termination_marker_size", verbose, val))
		{
			protocol_settings.termination_marker_size = val;
			custom = true;
		}

		if (custom)
		{
			vca_ret = vca_set_led_protocol(&protocol_settings);
			if (vca_ret != VCA_SUCCESS)
			{
				if (verbose)
					std::clog << "Failed to set the custom LED protocol settings.\n" << vca_last_os_error_string() << "\n";
				return;
			}
		}

		if (verbose)
			std::clog << "Successfully configured the LED protocol settings.\n";
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

	delete []mBackBuffer;
	delete []mLedData;

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

	if (dd.idVendor != ADVANTECH_VENDOR_ID || dd.idProduct != LED_CONTROLLER_PRODUCT_ID || dd.bDeviceClass != LIBUSB_CLASS_COMM)
	{
		return false; // Not an Advantech Innocore LED Controller, or for some strange reason, not a virtual COM port.
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
	return USBDevice::probeAfterOpening();
}
	
bool AIDevice::matchConfiguration(const Value &config)
{
	return USBDevice::matchConfiguration(config);
}

void AIDevice::loadConfiguration(const Value &config)
{
	const Value &protocol = config["led_protocol"];
	if (!protocol.IsNull())
	{
		configureLedProtocol(protocol, mVerbose);
	}

    /*
     *	Channel mapping stuff:
     *   [ OPC Channel, First OPC Pixel, First output pixel, Pixel count ]
     */
	const USBDevice::Value* config_map = findConfigMap(config);
	if (!config_map)
	{
		if (mVerbose)
			std::clog << "LED mapping not found in configuration.  It will not be possible to set any LED colours.\n";

		return;
	}

	const USBDevice::Value& map = *config_map;
	std::vector<vca_led_mapping_t> mapping;
	uint32_t total_leds = 0;
	for (int i = 0, e = map.Size(); i != e; i++)
	{
		const Value& inst = map[i];
		if (inst.IsArray() && inst.Size() == 4)
		{
			const Value& vChannel = inst[0u];
			const Value& vFirstOPC = inst[1];
			const Value& vFirstOut = inst[2];
			const Value& vCount = inst[3];
			if (!vChannel.IsUint() || !vFirstOPC.IsUint() || !vFirstOut.IsUint() || !vCount.IsInt())
			{
				std::clog << "LED mapping entry " << i << " has invalid values.  It will not be possible to set any LED colours.\n";
				return;
			}
			uint32_t channel = vChannel.GetUint();
			uint32_t firstOPC = vFirstOPC.GetUint();
			uint32_t firstOut = vFirstOut.GetUint();
			int32_t count = vCount.GetInt();

			if (channel != 0)
			{
				std::clog << "LED mapping entry " << i << " has a channel parameter that is not zero, this is not supported at this time.  It will not be possible to set any LED colours.\n";
				return;
			}

			if (firstOut >= VCA_MAX_LEDS_SUPPORTED)
			{
				std::clog << "LED mapping entry " << i << " has an output parameter (" << firstOut << ") that exceeds the number of LEDs supported by the LED Controller (" << VCA_MAX_LEDS_SUPPORTED << ").  It will not be possible to set any LED colours.\n";
				return;
			}

			int32_t output_channel = firstOut / VCA_MAX_LEDS_PER_CHANNEL;
			vcaS8 inc = 1;
			if (count < 0)
			{
				count = -count;
				inc = -1;
			}
			firstOut %= VCA_MAX_LEDS_PER_CHANNEL;
			while (count > 0)
			{
				int32_t channel_count = count;
				int32_t lastOut = (firstOut + ((count - 1) * inc));
				if (lastOut >= VCA_MAX_LEDS_PER_CHANNEL)
					channel_count = VCA_MAX_LEDS_PER_CHANNEL - firstOut;
				else if (lastOut < 0)
					channel_count += lastOut;

				vca_led_mapping_t entry = {(vcaU16)firstOPC, (vcaU16)channel_count, (vcaU8)output_channel, 0, (vcaU8)firstOut, inc};
				mapping.push_back(entry);

				count -= channel_count;
				output_channel += inc;
				firstOut = (inc == 1 ? 0 : 255);
				firstOPC += channel_count;
				total_leds += channel_count;

				if (count != 0 && (output_channel >= VCA_NUM_LED_CHANNELS || output_channel < 0))
				{
					std::clog << "LED mapping entry " << i << " extends outside the valid range of LED Controller channels (0 to " << VCA_NUM_LED_CHANNELS << ").  It will not be possible to set any LED colours.\n";
					return;
				}
			}
		}
	}

	int ret = vca_set_led_mapping(mapping.data(), mapping.size());
	if (VCA_SUCCESS != ret)
	{
		std::clog << "Failed to set LED mapping on the LED Controller (" << ret << ").  It will not be possible to set any LED colours.\n";
		return;
	}

	mLedDataSize = total_leds * COLOURS_PER_LED;
	mBackBuffer = new uint8_t[mLedDataSize];
	mLedData = new uint8_t[mLedDataSize];
	if (!mBackBuffer || !mLedData)
	{
		delete []mBackBuffer;
		delete []mLedData;
		mBackBuffer = nullptr;
		mLedData = nullptr;
		mLedDataSize = 0;
		std::clog << "Failed to initialise LED mapping - failed to allocate framebuffers.  It will not be possible to set any LED colours.\n";
		return;
	}

	if (mVerbose)
		std::clog << "LED mapping applied to the LED Controller.\n";
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
	if (!color.IsNull())
	{
		if (mVerbose)
			std::clog << "Setting simple gamma correction on the LED Controller.\n";
		int ret = vca_enable_simple_gamma_correction();
		if (mVerbose && VCA_SUCCESS != ret)
		{
			std::clog << "Failed to set simple gamma correction on the LED Controller (" << ret << ").\n";
		}
	}
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
		vca_set_mapped_led_colours(self->mLedData, self->mLedDataSize);
		vca_start_led_data_transmission();
		self->mFrameTransferInProgress = false;
	}
}

// Takes an incoming message and sets the framebuffer colours as appropriate
// For now, mapping is ignored, but will be implemented in the future
void AIDevice::opcSetPixelColors(const OPC::Message &msg)
{
	if (msg.length() > mLedDataSize)
	{
		if (mVerbose)
		{
			std::clog << "Framebuffer size (" << mLedDataSize << ") too small for incoming message size: " << msg.length() << "\n";
		}
		return;
	}
	if (msg.channel != 0)
	{
		if (mVerbose)
		{
			std::clog << "Only SetPixelColours commands for channel 0 are supported on Advantech Innocore LED Controllers." << "\n";
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

	memcpy(mLedData, mBackBuffer, mLedDataSize);
	mNewFrameReadyToSend = true;
}
