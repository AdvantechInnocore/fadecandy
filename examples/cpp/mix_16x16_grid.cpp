#include <cstdio>
#include <vector>

#include "lib/opc_client.h"

namespace
{
	const size_t grid_w = 16;
	const size_t grid_h = 16;
	const size_t grid_size = grid_w * grid_h;
	const size_t bytes_per_led = 3;
	const size_t frame_bytes = grid_size * bytes_per_led;
	const uint8_t i_1 = 0x3f;
	const uint8_t i_2 = 0x7f;
	const uint8_t i_0 = 0x00;

	struct Box
	{
		Box() = delete;
		Box(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) :
			box_x(x), box_y(y), box_w(w), box_h(h),
			r(r), g(g), b(b),
			dot_x(x), dot_y(y), dot_inc_x(1), dot_inc_y(0)
		{

		}

		void update()
		{
			const int end_x = calcEndX();
			const int end_y = calcEndY();
			if (dot_inc_x != 0)
			{
				dot_x += dot_inc_x;
				if (dot_x == end_x)
				{
					dot_inc_x = 0;
					dot_inc_y = 1;
				}
				else if (dot_x == box_x)
				{
					dot_inc_x = 0;
					dot_inc_y = -1;
				}
			}
			else
			{
				dot_y += dot_inc_y;
				if (dot_y == end_y)
				{
					dot_inc_x = -1;
					dot_inc_y = 0;
				}
				else if (dot_y == box_y)
				{
					dot_inc_x = 1;
					dot_inc_y = 0;
				}
			}
		}

		void draw(uint8_t* led_data)
		{
			for (int y = box_y, end_y = calcEndY(); y <= end_y; ++y)
			{
				for (int x = box_x, end_x = calcEndX(); x <= end_x; ++x)
				{
					if (x == box_x || x == end_x || y == box_y || y == end_y)
					{
						uint8_t* led = led_data + (bytes_per_led * (x + (y * grid_w)));
						*led++ = r; *led++ = g; *led = b;
					}
				}
			}
			uint8_t* led = led_data + (bytes_per_led * (dot_x + (dot_y * grid_w)));
			*led++ = r * 3; *led++ = g * 3; *led = b * 3;
		}

		int calcEndX()
		{
			return box_x + box_w - 1;
		}

		int calcEndY()
		{
			return box_y + box_h - 1;
		}

		int box_x;
		int box_y;
		int box_w;
		int box_h;
		uint8_t r;
		uint8_t g;
		uint8_t b;

		int dot_x;
		int dot_y;
		int dot_inc_x;
		int dot_inc_y;
	};
}

int main(int argc, char **argv)
{
	OPCClient opc;
	std::vector<uint8_t> frame_buffer;
	std::vector<Box> boxes;

	boxes.push_back({0, 0, 16, 16, i_1, i_0, i_0});
	boxes.push_back({1, 1, 14, 14, i_0, i_1, i_0});
	boxes.push_back({2, 2, 12, 12, i_1, i_1, i_0});
	boxes.push_back({3, 3, 10, 10, i_0, i_0, i_1});
	boxes.push_back({4, 4,  8,  8, i_1, i_0, i_1});
	boxes.push_back({5, 5,  6,  6, i_0, i_1, i_1});
	boxes.push_back({6, 6,  4,  4, i_1, i_1, i_1});
	boxes.push_back({7, 7,  2,  2, i_1, i_0, i_0});

	frame_buffer.resize(sizeof(OPCClient::Header) + frame_bytes);
	OPCClient::Header::view(frame_buffer).init(0, opc.SET_PIXEL_COLORS, frame_bytes);

	if (!opc.resolve("localhost"))
	{
		printf("Resolve failed.\n");
		return 1;
	}

	if (!opc.tryConnect())
	{
		printf("Try connect failed.\n");
		return 1;
	}

	do
	{
		uint8_t* led_rgb = OPCClient::Header::view(frame_buffer).data();
		for (auto& box : boxes)
		{
			box.draw(led_rgb);
			box.update();
		}

		opc.write(frame_buffer);
		usleep(66667);
	} while (true);


	printf("All done.\n");
	return 0;
}
