#ifndef MESHCUTER_TYPES_H
#define MESHCUTER_TYPES_H

// This header exists because the Arduino IDE auto-generates
// function prototypes at the top of the .ino file, before any
// struct definitions. By placing structs in a header that is
// #included at the top, they are available when the prototypes
// are generated.

typedef struct {
    int id_x, id_w;
    int rssi_x, rssi_w;
    int name_x, name_w;
    int extra_x, extra_w;
} ColLayout;

#endif
