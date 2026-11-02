#!/usr/bin/env python3
import os
import sys
import numpy as np

base_dir = os.path.dirname(os.path.abspath(__file__))

print("=== Generating all benchmark dataset .npy files ===")

# 1. ADD: 1M (1048576)
add_dir = os.path.join(base_dir, "add")
os.makedirs(add_dir, exist_ok=True)
dim_add = 1024 * 1024
np.random.seed(1113)
add_in1 = np.random.rand(dim_add).astype('float16')
add_in2 = np.random.rand(dim_add).astype('float16')
add_out = (add_in1 + add_in2).astype('float16')
np.save(os.path.join(add_dir, f"resadd_input0_{dim_add}.npy"), add_in1)
np.save(os.path.join(add_dir, f"resadd_input1_{dim_add}.npy"), add_in2)
np.save(os.path.join(add_dir, f"resadd_output_{dim_add}.npy"), add_out)
print(f"Generated ADD 1M in {add_dir}")

# 2. MUL: 1M (1048576) and 2M (2097152)
mul_dir = os.path.join(base_dir, "mul")
os.makedirs(mul_dir, exist_ok=True)
for dim_mul in [1024 * 1024, 2 * 1024 * 1024]:
    np.random.seed(1113)
    mul_in1 = np.random.rand(dim_mul).astype('float16')
    mul_in2 = np.random.rand(dim_mul).astype('float16')
    mul_out = (mul_in1 * mul_in2).astype('float16')
    np.save(os.path.join(mul_dir, f"eltmul_input0_{dim_mul}.npy"), mul_in1)
    np.save(os.path.join(mul_dir, f"eltmul_input1_{dim_mul}.npy"), mul_in2)
    np.save(os.path.join(mul_dir, f"eltmul_output_{dim_mul}.npy"), mul_out)
    print(f"Generated MUL {dim_mul} in {mul_dir}")

# 3. RELU: 1M (1048576) and 4M (4194304)
relu_dir = os.path.join(base_dir, "relu")
os.makedirs(relu_dir, exist_ok=True)
for dim_relu in [1024 * 1024, 4 * 1024 * 1024]:
    np.random.seed(1113)
    relu_in = np.random.randn(dim_relu).astype('float16')
    relu_out = (np.abs(relu_in) * (relu_in > 0)).astype('float16')
    np.save(os.path.join(relu_dir, f"relu_input_{dim_relu}.npy"), relu_in)
    np.save(os.path.join(relu_dir, f"relu_output_{dim_relu}.npy"), relu_out)
    print(f"Generated RELU {dim_relu} in {relu_dir}")

# 4. GEMV: 4096x1024 (original smoke) and 4096x4096 (paper dimension)
gemv_dir = os.path.join(base_dir, "gemv")
os.makedirs(gemv_dir, exist_ok=True)
for dim_out, dim_in in [(4096, 1024), (4096, 4096)]:
    batch = 1
    np.random.seed(1113)
    batch_in = np.random.standard_normal(size=(dim_in, batch)).astype('float16')
    data_w = np.random.standard_normal(size=(dim_out, dim_in)).astype('float16')
    np.random.shuffle(data_w)
    batch_out = np.matmul(data_w, batch_in).astype('float16')
    
    batch_in_t = batch_in.T.copy()
    batch_out_t = batch_out.T.copy()
    
    tag = f"{dim_out}x{dim_in}"
    np.save(os.path.join(gemv_dir, f"gemv_input_{tag}.npy"), batch_in_t)
    np.save(os.path.join(gemv_dir, f"gemv_weight_{tag}.npy"), data_w)
    np.save(os.path.join(gemv_dir, f"gemv_output_{tag}.npy"), batch_out_t)
    print(f"Generated GEMV {tag} in {gemv_dir}")

print("=== All datasets generated successfully ===")

