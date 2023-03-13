#include "aidevice.h"

#include <iostream>

#include <libusbi.h>

#include <vca_api.h>

AIDevice::AIDevice(libusb_device *device, bool verbose)
	: USBDevice(device, "Advantech Innocore LED Controller", verbose)
{
}

AIDevice::~AIDevice()
{
	vca_disconnect();
}

bool AIDevice::probe(libusb_device *device)
{
	libusb_device_descriptor dd;

	if (libusb_get_device_descriptor(device, &dd) < 0)
	{
		return false;	// Can't access descriptor?
	}

	if (dd.bDeviceClass != LIBUSB_CLASS_COMM)
	{
		return false;	// Not a COM port device, ie not an Advantech Innocore LED Controller
	}

	char* connected_port = nullptr;
	if (vca_is_connected(&connected_port))
	{
		return false;	// Already connected, only one connection is currently supported
	}

	char com_port[32];
	for (int p = 0; p < 256; ++p)
	{
		sprintf(com_port, "COM%d", p);
		int ret = vca_connect(com_port);
		std::cout << "vca_connect result for COM" << p << ":" << ret << "\n";

		if (ret == VCA_SUCCESS)
		{
			return true;
		}
	}

	return false;
}

int AIDevice::open()
{
	return 0;	// Already open
}
	
void AIDevice::loadConfiguration(const Value &config)
{
	std::cout << "MICK: implement loadConfiguration()\n";
}
	
void AIDevice::writeMessage(const OPC::Message &msg)
{
	std::cout << "MICK: implement writeMessage()\n";
}
	
std::string AIDevice::getName()
{
	std::cout << "MICK: implement getName()\n";
	return "Bob";
}
	
void AIDevice::flush()
{
	std::cout << "MICK: implement flush()\n";
}	
