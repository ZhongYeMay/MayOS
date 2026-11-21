#ifndef MAYOS_XHCI_H
#define MAYOS_XHCI_H

/* Initialize standards-compliant PCI xHCI controllers.  Input remains on
 * the PS/2 fallback until USB device enumeration is enabled. */
int xhci_init(void);
int xhci_controller_count(void);
int xhci_connected_port_count(void);

#endif
