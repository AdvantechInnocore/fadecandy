/*
 * Model creation script for a 32x16 grid made of zig-zag 8x8 grids.
 *
 * 2014 Micah Elizabeth Scott
 * This file is released into the public domain.
 */

var model = []
var scale = -1 / 8.0;
var centerX = 31 / 2.0;
var centerY = 15 / 2.0;

// One big grid
var index = 0;
for (var x = 0; x < 32; x++) {
	for (var y = 0; y < 16; y++) {
		var ziggy_y = (x & 1) ? (15 - y) : (y);
		model[index++] = {
			point: [ (x - centerX) * scale, 0, (ziggy_y - centerY) * scale ]
		};
	}
}

console.log(JSON.stringify(model));
