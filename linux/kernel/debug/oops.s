; Kernel Oops Handler
k3_oops:
    call k3_dump_stack
    ret
