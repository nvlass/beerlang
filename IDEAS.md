# Future Ideas

## Make beerlang embeddable

As a shared library / set of files that can be compiled in a project

## Make vms serializable

The idea is to be able to store / load the execution state, or even
"pass" the execution to another process.

Of course, this has some restrictions, e.g. we should be able to
serialize and send *pure* vms, i.e. vms that don't perform / handle
IO.

Several things are possible with this approach -- prepare VMs and send
them elsewhere for execution.
