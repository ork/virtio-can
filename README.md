virtio-can
==========

A CAN bus controller driver using virtio.

Why ?
-----

We want to provide access to a CAN controllers to guests running on a
hypervisor. Some of these guests may be real-time systems, so the specification
of the virtio interface for CAN must contain informations allowing a
deterministic usage.

This driver is the Linux device driver for the virtio-can virtual device, it
will be the starting point to specify the virtio-can interface.

Design decisions
----------------

What we need is an API abstracting the inner working of a CAN controller to a
virtualized device while still having a good performance.

We have to provide the guest module with some information about its paramaters
provided by the host:
- Number of mailboxes
- Endianness
- CAN-id filters

Currently, these parameters will be statically defined in a device-tree file,
or as a module loading parameter.

One of the things to keep in mind is that we want to run real-time tasks at
some time, so the TX FIFOs have to be implemented in the host to provide the
guests with a good responsivity. For the systems that don't support FIFOs, we
can use the built-in virtio ring buffer to share the device while writing.

The Linux driver
----------------

This driver will connect to the virtual device, setup the queues and mailboxes
according to the parameters, and manage the network device for the SocketCAN
subsystem.
