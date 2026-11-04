; Cooperative sched_yield primitive
k3_sched_yield:
    call k3_schedule_next
    ret
