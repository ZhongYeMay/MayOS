#ifndef MAYOS_XHCI_H
#define MAYOS_XHCI_H

typedef struct { int dx, dy, buttons, changed, key; } XhciInput;

int xhci_init(void);
int xhci_controller_count(void);
int xhci_connected_port_count(void);
int xhci_boot_input_count(int protocol);
void xhci_poll(XhciInput *input);

#endif
