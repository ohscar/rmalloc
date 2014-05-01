#!/usr/bin/env python

import sys
import random
import numpy as np
import matplotlib.mlab as mlab
import matplotlib.pyplot as plt
import os.path

# reference value color first
COLORS = ["b-", "r-", "g-", "m-", "b-", "y-", "c-"]
#COLORS = ["k-", "k:", "k--", "k_", "k", "k", "k-."]
#COLORS = ["k--", "g", "r-.", "y_", "k ", "k", "k-."]
#COLORS = ["k--", "g", "r-.", "y_", "k ", "k", "k-."]
g_figure = None
g_figure_nr = 1

g_multiple_figure = None
g_multiple_figure_nr = 1

def formatter_x_to_percent(x, pos):
    return "{0}".format(round(x*100.0, 2))

def formatter_max_to_percent(y, pos, ymax):
    p = float(y)/ymax * 100.0
    return "{0}\n{1}%".format(y, round(p, 2))

def plot_init_multiple():
    global g_multiple_figure
    g_multiple_figure = plt.figure(figsize=(19.2, 12.0), dpi=300)
    #plt.axis('tight')
    #plt.grid(True)

def plot_save_multiple(app):
    fname = app + ".png"
    print "Saving plot to", fname
    plt.savefig(fname)

def subplot_multiple(t, xl, yl):
    global g_multiple_figure_nr

    ax = g_multiple_figure.add_subplot(2, 2, g_multiple_figure_nr, xlabel=xl, ylabel=yl, title=t)
    g_multiple_figure_nr += 1

    return ax

FLOAT_SPEED = 1
SINK_SPEED = 0.5
PERCENT = 30

def plot_do():
    #g_multiple_figure.suptitle("Lifetime")

    float_speed, sink_speed, percent = FLOAT_SPEED, SINK_SPEED, PERCENT
    generate_lifetime(float_speed, sink_speed, percent)

    float_speed, sink_speed, percent = FLOAT_SPEED, SINK_SPEED, 50
    generate_lifetime(float_speed, sink_speed, percent)

    float_speed, sink_speed, percent = 1, 1, PERCENT
    generate_lifetime(float_speed, sink_speed, percent)

    float_speed, sink_speed, percent = 1, 2, PERCENT
    generate_lifetime(float_speed, sink_speed, percent)

    #plots.append(plot_time)
    #plt.legend(plots, [plot.get_label() for plot in plots], loc=2) # loc= 2=upper left, 4=lower right, 3=lower left

LOWER = -50
UPPER = 50
POINTS = 1000
#THEHANDLE = (UPPER-LOWER)/2 + LOWER
THEHANDLE = (POINTS/50)
color_counter = 0 # make global
def generate_lifetime(float_speed, sink_speed, percent):
    global color_counter
    values = [random.randint(LOWER, UPPER) for i in range(POINTS)]
    lifetime = []

    life = 0
    for i in range(len(values)):
        #h = values[i]
        #thishandle = i % THEHANDLE == 0
        thishandle = random.randint(0, 100) < percent
        if thishandle:
            life += float_speed
        else:
            if life >= sink_speed:
                life -= sink_speed

        lifetime.append(life)

    #lifetime = values[:]

    plot_color = COLORS[color_counter]
    color_counter += 1
    ax = subplot_multiple("Lifetime #%d: FLOAT %.2f SINK %.2f PERCENT %d%%" % (color_counter, float_speed, sink_speed, percent), "", "")
    title = "Float %.2f, sink %.2f" % (float_speed, sink_speed)
    plot_time, = ax.plot(lifetime, plot_color, label='%s' % (title))


def main():
    plot_init_multiple()
    print "plot"
    plot_do()
    print "save"
    plot_save_multiple("../graphics/locking-lifetime-explanation")


if __name__ == '__main__':
    main()


