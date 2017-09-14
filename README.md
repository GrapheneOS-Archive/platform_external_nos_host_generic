# Generic host components for Nugget

Nugget will be used in different contexts and with different hosts. This repo
contains the components that can be shared between those hosts.

## `nugget`

The `nugget` directory contains items that are shared between the host and the
firmware. Those include:

   * shared headers
   * service protos

## `libnos`

`libnos` is a C++ library for communication with a Nugget device. It offers an
interface to manage a connection and exchange data and a generator for RPC stubs
based on service protos.
