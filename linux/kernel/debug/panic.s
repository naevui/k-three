; Kernel Panic Handler
k3_panic:
    li r0, str_panic_msg
    call k3_kputs
    halt
str_panic_msg: .string "Kernel Panic!\n"
