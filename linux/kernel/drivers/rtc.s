; Hardware Real-Time Clock Driver
k3_rtc_read_time:
    call k3_get_time_ms
    ret
