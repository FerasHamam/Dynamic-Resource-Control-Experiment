sudo ovs-vsctl -- set port s1-eth3 qos=@newqos \
    -- --id=@newqos create qos type=linux-htb other-config:max-rate=400000000 queues=0=@default,1=@queue1,2=@queue2 \
    -- --id=@default create queue other-config:min-rate=400000000 other-config:max-rate=400000000 \
    -- --id=@queue1 create queue other-config:min-rate=200000000 other-config:max-rate=400000000 other-config:priority=1 \
    -- --id=@queue2 create queue other-config:max-rate=200000000 other-config:min-rate=200000000 other-config:priority=2
