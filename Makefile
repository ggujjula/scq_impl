.PHONY: all

CFLAGS=-Og -g

all: scq_driver

scq_driver: scq.o ring_buffer.o
