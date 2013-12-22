#!/usr/bin/env python

import Image, ImageDraw
import sys
from math import ceil

IMAGE_WIDTH = 800
IMAGE_HEIGHT = 600
PIXEL_WIDTH = 5
PIXEL_HEIGHT = 5

COLORS = {' ': (0, 255, 0),
          '#': (255, 0, 0),
          '.': (0, 0, 0)
         }

#
# App
#
def main():
    usage = "usage: %s input.txt output.png" % sys.argv[0]
    if len(sys.argv) < 3:
        print usage
        sys.exit(1)

    try:
        f = open(sys.argv[1], "rt")
    except:
        print "couldn't open file %s for reading." % sys.argv[1]
        print usage
        sys.exit(1)

    output = sys.argv[2]

    d = f.read()

    xres = IMAGE_WIDTH
    xpixels = xres/PIXEL_WIDTH
    yres = int(ceil(len(d)/float(xpixels)) * PIXEL_HEIGHT)
    if yres < IMAGE_HEIGHT:
        yres = IMAGE_HEIGHT

    img = Image.new("RGB", (xres, yres), "white")
    draw = ImageDraw.Draw(img)
    x = 0
    y = 0
    for i in range(len(d)):
        try:
            color = COLORS[d[i]]
        except:
            continue
        draw.rectangle((x, y, x+PIXEL_WIDTH, y+PIXEL_HEIGHT), fill=color, outline=color)

        x += PIXEL_WIDTH
        if x and x % xres == 0:
            x = 0
            y += PIXEL_HEIGHT

    img.save(output, "PNG")

if __name__ == '__main__':
    main()


