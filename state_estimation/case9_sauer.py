import numpy as np

def case9_sauer():
    """
    9节点系统数据（Sauer模型）
    
    返回:
        pp_net: pandapower网络对象
        gen_bus: 发电机母线索引
        PM: 机械功率向量
        M: 惯性常数向量
        D: 阻尼系数向量
    """
    import pandapower as pp
    
    # 创建空网络
    net = pp.create_empty_network()
    
    # 母线数据
    pp.create_bus(net, vn_kv=230, name="Bus 1 (Generator 1)")
    pp.create_bus(net, vn_kv=230, name="Bus 2 (Generator 2)")
    pp.create_bus(net, vn_kv=230, name="Bus 3 (Generator 3)")
    pp.create_bus(net, vn_kv=230, name="Bus 4")
    pp.create_bus(net, vn_kv=230, name="Bus 5")
    pp.create_bus(net, vn_kv=230, name="Bus 6")
    pp.create_bus(net, vn_kv=115, name="Bus 7")
    pp.create_bus(net, vn_kv=115, name="Bus 8")
    pp.create_bus(net, vn_kv=115, name="Bus 9")
    
    # 发电机数据
    pp.create_gen(net, bus=0, p_mw=163, vm_pu=1.0487, name="Generator 1", slack=True)
    pp.create_gen(net, bus=1, p_mw=85, vm_pu=1.0316, name="Generator 2")
    pp.create_gen(net, bus=2, p_mw=215, vm_pu=1.0278, name="Generator 3")
    
    # 负荷数据
    pp.create_load(net, bus=4, p_mw=90, q_mvar=30, name="Load 5")
    pp.create_load(net, bus=7, p_mw=100, q_mvar=35, name="Load 8")
    pp.create_load(net, bus=8, p_mw=125, q_mvar=50, name="Load 9")
    
    # 变压器数据 (230kV/115kV)
    pp.create_transformer_from_parameters(
        net, hv_bus=3, lv_bus=6, sn_mva=100, vn_hv_kv=230, vn_lv_kv=115,
        vkr_percent=0.87, vk_percent=16, pfe_kw=100, i0_percent=0.1
    )
    pp.create_transformer_from_parameters(
        net, hv_bus=5, lv_bus=8, sn_mva=100, vn_hv_kv=230, vn_lv_kv=115,
        vkr_percent=0.87, vk_percent=16, pfe_kw=100, i0_percent=0.1
    )
    
    # 线路数据 (230kV)
    pp.create_line_from_parameters(
        net, from_bus=0, to_bus=3, length_km=15, r_ohm_per_km=0.0198, 
        x_ohm_per_km=0.0594, c_nf_per_km=13.2, max_i_ka=0.5
    )
    pp.create_line_from_parameters(
        net, from_bus=3, to_bus=5, length_km=40, r_ohm_per_km=0.0198, 
        x_ohm_per_km=0.0594, c_nf_per_km=13.2, max_i_ka=0.5
    )
    pp.create_line_from_parameters(
        net, from_bus=1, to_bus=6, length_km=10, r_ohm_per_km=0.0396, 
        x_ohm_per_km=0.1188, c_nf_per_km=6.6, max_i_ka=0.5
    )
    pp.create_line_from_parameters(
        net, from_bus=2, to_bus=8, length_km=30, r_ohm_per_km=0.0198, 
        x_ohm_per_km=0.0594, c_nf_per_km=13.2, max_i_ka=0.5
    )
    
    # 线路数据 (115kV)
    pp.create_line_from_parameters(
        net, from_bus=6, to_bus=7, length_km=20, r_ohm_per_km=0.0792, 
        x_ohm_per_km=0.2376, c_nf_per_km=3.3, max_i_ka=0.5
    )
    pp.create_line_from_parameters(
        net, from_bus=6, to_bus=4, length_km=15, r_ohm_per_km=0.0792, 
        x_ohm_per_km=0.2376, c_nf_per_km=3.3, max_i_ka=0.5
    )
    pp.create_line_from_parameters(
        net, from_bus=7, to_bus=8, length_km=25, r_ohm_per_km=0.0792, 
        x_ohm_per_km=0.2376, c_nf_per_km=3.3, max_i_ka=0.5
    )
    
    # 发电机参数（动态模型）
    gen_bus = np.array([0, 1, 2])  # 发电机所在母线索引
    PM = np.array([163.0, 85.0, 215.0])  # 机械功率 (MW)
    M = np.array([10.0, 10.0, 10.0])  # 惯性常数 (s)
    D = np.array([1.0, 1.0, 1.0])  # 阻尼系数
    
    return net, gen_bus, PM, M, D
