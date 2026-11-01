/***************************************************************************************************
 * Copyright (C) 2021 Samsung Electronics Co. LTD
 *
 * This software is a property of Samsung Electronics.
 * No part of this software, either material or conceptual may be copied or distributed,
 * transmitted, transcribed, stored in a retrieval system, or translated into any human
 * or computer language in any form by any means,electronic, mechanical, manual or otherwise,
 * or disclosed to third parties without the express written permission of Samsung Electronics.
 * (Use of the Software is restricted to non-commercial, personal or academic, research purpose
 * only)
 **************************************************************************************************/

#include "tests/KernelTestCases.h"

#include "gtest/gtest.h"
#include "tests/PIMKernel.h"

/*
 * PIMKernelTest:
 * functionality test for PIM Kernel function
 */

using namespace DRAMSim;

TEST_F(PIMKernelFixture, gemv_tree)
{
    shared_ptr<PIMKernel> kernel = make_pim_kernel();

    uint32_t batch_size = 1;
    uint32_t output_dim = 4096;
    uint32_t input_dim = 1024;

    DataDim *dim_data = new DataDim(KernelType::GEMVTREE, batch_size, output_dim, input_dim, true);
    dim_data->printDim(KernelType::GEMVTREE);

    int numInputTile = ceil((double)dim_data->weight_npbst_.bShape[1] / (double)8);

    result_ = new BurstType[output_dim * numInputTile];
    reduced_result_ = new BurstType[output_dim / 16];

    kernel->preloadGemv(&dim_data->weight_npbst_);
    kernel->executeGemv(&dim_data->weight_npbst_, &dim_data->input_npbst_, true);
    unsigned end_col = kernel->getResultColGemv(dim_data->dimTobShape(input_dim), output_dim);
    kernel->readResult(result_, pimBankType::ODD_BANK, output_dim * numInputTile, 0, 0, end_col);
    kernel->runPIM();

    fp16 *temp_fp16 = new fp16[numInputTile];

    for (int i = 0; i < output_dim; i++)
    {
        kernel->adderTree(&result_[i], output_dim, numInputTile, 0, temp_fp16);
        EXPECT_FP16_EQ(temp_fp16[0], dim_data->output_npbst_.getBurst(0).fp16Data_[i]);
        reduced_result_[i / 16].fp16Data_[i % 16] = temp_fp16[0];
    }

    delete[] result_;
    delete[] reduced_result_;
    delete[] temp_fp16;
    delete dim_data;
}

TEST_F(PIMKernelFixture, gemv)
{
    shared_ptr<PIMKernel> kernel = make_pim_kernel();

    uint32_t batch_size = 1;
    uint32_t output_dim = 4096;
    uint32_t input_dim = 1024;

    DataDim *dim_data = new DataDim(KernelType::GEMV, batch_size, output_dim, input_dim, true);
    dim_data->printDim(KernelType::GEMV);

    reduced_result_ = new BurstType[dim_data->dimTobShape(output_dim)];
    result_ = getResultPIM(KernelType::GEMV, dim_data, kernel, result_);

    testStatsClear();
    expectAccuracy(KernelType::GEMV, output_dim, dim_data->output_npbst_,
                   dim_data->getNumElementsPerBlocks());

    delete[] result_;
    delete[] reduced_result_;
    delete dim_data;
}

TEST_F(PIMKernelFixture, mul)
{
    shared_ptr<PIMKernel> kernel = make_pim_kernel();

    uint32_t batch_size = 1;
    uint32_t output_dim = 1024 * 1024;
    uint32_t input_dim = output_dim;

    DataDim *dim_data = new DataDim(KernelType::MUL, batch_size, output_dim, input_dim, true);
    dim_data->printDim(KernelType::MUL);

    result_ = getResultPIM(KernelType::MUL, dim_data, kernel, result_);
    kernel->runPIM();

    testStatsClear();
    expectAccuracy(KernelType::MUL, dim_data->dimTobShape(output_dim), dim_data->output_npbst_);

    delete[] result_;
    delete dim_data;
}

TEST_F(PIMKernelFixture, add)
{
    shared_ptr<PIMKernel> kernel = make_pim_kernel();

    uint32_t batch_size = 1;
    uint32_t output_dim = 1024 * 1024;
    uint32_t input_dim = output_dim;

    DataDim *dim_data = new DataDim(KernelType::ADD, batch_size, output_dim, input_dim, true);
    dim_data->printDim(KernelType::ADD);

    result_ = getResultPIM(KernelType::ADD, dim_data, kernel, result_);
    kernel->runPIM();
    reportCycles(kernel, "add [baseline]");

    testStatsClear();
    expectAccuracy(KernelType::ADD, dim_data->dimTobShape(output_dim), dim_data->output_npbst_);

    delete[] result_;
    delete dim_data;
}

TEST_F(PIMKernelFixture, relu)
{
    shared_ptr<PIMKernel> kernel = make_pim_kernel();
    uint32_t output_dim = 1024 * 1024;
    uint32_t input_dim = output_dim;

    DataDim *dim_data = new DataDim(KernelType::RELU, 1, output_dim, input_dim, true);
    dim_data->printDim(KernelType::RELU);

    result_ = getResultPIM(KernelType::RELU, dim_data, kernel, result_);
    kernel->runPIM();
    reportCycles(kernel, "relu_paper_4m [baseline]");

    testStatsClear();
    expectAccuracy(KernelType::RELU, dim_data->dimTobShape(output_dim), dim_data->output_npbst_);

    delete[] result_;
    delete dim_data;
}

TEST_F(PIMKernelFixture, gemv_pim_act)
{
    shared_ptr<PIMKernel> kernel = make_pim_kernel();

    uint32_t batch_size = 1;
    uint32_t output_dim = 4096;
    uint32_t input_dim = 4096;

    DataDim *dim_data = new DataDim(KernelType::GEMV, batch_size, output_dim, input_dim, true);
    dim_data->printDim(KernelType::GEMV);

    reduced_result_ = new BurstType[dim_data->dimTobShape(output_dim)];
    result_ = getResultPIMACT(KernelType::GEMV, dim_data, kernel, result_);
    reportCycles(kernel, "gemv_pim_act [PIM-ACT]");

    testStatsClear();
    expectAccuracy(KernelType::GEMV, output_dim, dim_data->output_npbst_,
                   dim_data->getNumElementsPerBlocks());

    delete[] result_;
    delete[] reduced_result_;
    delete dim_data;
}

TEST_F(PIMKernelFixture, add_pim_act)
{
    shared_ptr<PIMKernel> kernel = make_pim_kernel();

    uint32_t batch_size = 1;
    uint32_t output_dim = 1024 * 1024;
    uint32_t input_dim = output_dim;

    DataDim *dim_data = new DataDim(KernelType::ADD, batch_size, output_dim, input_dim, true);
    dim_data->printDim(KernelType::ADD);

    result_ = getResultPIMACT(KernelType::ADD, dim_data, kernel, result_);
    kernel->runPIM();
    reportCycles(kernel, "add_pim_act [PIM-ACT]");

    testStatsClear();
    expectAccuracy(KernelType::ADD, dim_data->dimTobShape(output_dim), dim_data->output_npbst_);

    delete[] result_;
    delete dim_data;
}

TEST_F(PIMKernelFixture, mul_pim_act)
{
    shared_ptr<PIMKernel> kernel = make_pim_kernel();

    uint32_t batch_size = 1;
    uint32_t output_dim = 2 * 1024 * 1024;
    uint32_t input_dim = output_dim;

    DataDim *dim_data = new DataDim(KernelType::MUL, batch_size, output_dim, input_dim, true);
    dim_data->printDim(KernelType::MUL);

    result_ = getResultPIMACT(KernelType::MUL, dim_data, kernel, result_);
    kernel->runPIM();
    reportCycles(kernel, "mul_pim_act [PIM-ACT]");

    testStatsClear();
    expectAccuracy(KernelType::MUL, dim_data->dimTobShape(output_dim), dim_data->output_npbst_);

    delete[] result_;
    delete dim_data;
}

TEST_F(PIMKernelFixture, relu_pim_act)
{
    shared_ptr<PIMKernel> kernel = make_pim_kernel();
    uint32_t output_dim = 4 * 1024 * 1024;
    uint32_t input_dim = output_dim;

    DataDim *dim_data = new DataDim(KernelType::RELU, 1, output_dim, input_dim, true);
    dim_data->printDim(KernelType::RELU);

    result_ = getResultPIMACT(KernelType::RELU, dim_data, kernel, result_);
    kernel->runPIM();
    reportCycles(kernel, "relu_pim_act [PIM-ACT]");

    testStatsClear();
    expectAccuracy(KernelType::RELU, dim_data->dimTobShape(output_dim), dim_data->output_npbst_);

    delete[] result_;
    delete dim_data;
}
// ============================================================
// A1: Paper-size BASELINE tests (Section 6, Lines 998-999)
// These mirror the paper dimensions for the standard HBM-PIM
// baseline protocol. Original smoke tests above are preserved
// so quick sanity checks remain fast.
// ============================================================

TEST_F(PIMKernelFixture, gemv_paper_4kx4k)
{
    // A1: Paper Section 6 specifies GEMV as 4K x 4K.
    // The existing 'gemv' test uses 4096 x 1024 (wrong size for paper comparison).
    // This test runs the SAME baseline (standard HBM-PIM) protocol at the correct size.
    shared_ptr<PIMKernel> kernel = make_pim_kernel();

    uint32_t batch_size = 1;
    uint32_t output_dim = 4096;
    uint32_t input_dim  = 4096;  // Paper: 4K x 4K

    DataDim *dim_data = new DataDim(KernelType::GEMV, batch_size, output_dim, input_dim, true);
    dim_data->printDim(KernelType::GEMV);

    reduced_result_ = new BurstType[dim_data->dimTobShape(output_dim)];
    result_ = getResultPIM(KernelType::GEMV, dim_data, kernel, result_);
    reportCycles(kernel, "gemv_paper_4kx4k [baseline]");

    testStatsClear();
    expectAccuracy(KernelType::GEMV, output_dim, dim_data->output_npbst_,
                   dim_data->getNumElementsPerBlocks());

    delete[] result_;
    delete[] reduced_result_;
    delete dim_data;
}

TEST_F(PIMKernelFixture, mul_paper_2m)
{
    // A1: Paper Section 6 specifies MUL as 2M vectors.
    // The existing 'mul' test uses 1M (wrong size for paper comparison).
    // This test runs the SAME baseline (standard HBM-PIM) protocol at the correct size.
    shared_ptr<PIMKernel> kernel = make_pim_kernel();

    uint32_t batch_size = 1;
    uint32_t output_dim = 2 * 1024 * 1024;  // Paper: 2M
    uint32_t input_dim  = output_dim;

    DataDim *dim_data = new DataDim(KernelType::MUL, batch_size, output_dim, input_dim, true);
    dim_data->printDim(KernelType::MUL);

    result_ = getResultPIM(KernelType::MUL, dim_data, kernel, result_);
    kernel->runPIM();
    reportCycles(kernel, "mul_paper_2m [baseline]");

    testStatsClear();
    expectAccuracy(KernelType::MUL, dim_data->dimTobShape(output_dim), dim_data->output_npbst_);

    delete[] result_;
    delete dim_data;
}

TEST_F(PIMKernelFixture, relu_paper_4m)
{
    // A1: Paper Section 6 specifies ReLU as 4M vectors.
    // The existing 'relu' test uses 1M (wrong size for paper comparison).
    // This test runs the SAME baseline (standard HBM-PIM) protocol at the correct size.
    shared_ptr<PIMKernel> kernel = make_pim_kernel();

    uint32_t output_dim = 4 * 1024 * 1024;  // Paper: 4M
    uint32_t input_dim  = output_dim;

    DataDim *dim_data = new DataDim(KernelType::RELU, 1, output_dim, input_dim, true);
    dim_data->printDim(KernelType::RELU);

    result_ = getResultPIM(KernelType::RELU, dim_data, kernel, result_);
    kernel->runPIM();
    reportCycles(kernel, "relu_paper_4m [baseline]");

    testStatsClear();
    expectAccuracy(KernelType::RELU, dim_data->dimTobShape(output_dim), dim_data->output_npbst_);

    delete[] result_;
    delete dim_data;
}

// ============================================================
// A3 (Step 4): Cross-protocol result verification tests
// Each test runs BOTH baseline and PIM-ACT on identical data
// and asserts the result vectors are FP16-bitwise-identical.
// These also report cycle counts for both modes so the Step 3
// 12-configuration matrix can be collected in a single run.
// Run with:
//   AAO=0 ./sim --gtest_filter="*a3*"   (PIM-ACT w/o AAO)
//   AAO=1 ./sim --gtest_filter="*a3*"   (PIM-ACT w/ AAO)
// ============================================================

TEST_F(PIMKernelFixture, a3_gemv_4kx4k)
{
    // A3/Step4: GEMV 4K x 4K cross-protocol correctness + cycle comparison.
    // Both kernels must produce bitwise-identical FP16 outputs.
    uint32_t batch_size = 1;
    uint32_t output_dim = 4096;
    uint32_t input_dim  = 4096;

    // --- Baseline run ---
    shared_ptr<PIMKernel> kernel_base = make_pim_kernel();
    DataDim *dim_base = new DataDim(KernelType::GEMV, batch_size, output_dim, input_dim, true);
    reduced_result_ = new BurstType[dim_base->dimTobShape(output_dim)];
    BurstType* base_result = getResultPIM(KernelType::GEMV, dim_base, kernel_base, nullptr);
    reportCycles(kernel_base, "gemv_4kx4k [baseline]");

    // --- PIM-ACT run (same DataDim, fresh kernel) ---
    shared_ptr<PIMKernel> kernel_pact = make_pim_kernel();
    DataDim *dim_pact = new DataDim(KernelType::GEMV, batch_size, output_dim, input_dim, true);
    BurstType* pact_result = getResultPIMACT(KernelType::GEMV, dim_pact, kernel_pact, nullptr);
    reportCycles(kernel_pact, "gemv_4kx4k [PIM-ACT]");

    // A3 check: results must match
    verifyResultsMatch(base_result, pact_result, output_dim * batch_size, "gemv_4kx4k");

    testStatsClear();
    delete[] base_result;
    delete[] pact_result;
    delete[] reduced_result_;
    delete dim_base;
    delete dim_pact;
}

TEST_F(PIMKernelFixture, a3_add_1m)
{
    // A3/Step4: ADD 1M cross-protocol correctness + cycle comparison.
    uint32_t batch_size = 1;
    uint32_t output_dim = 1024 * 1024;
    uint32_t input_dim  = output_dim;

    shared_ptr<PIMKernel> kernel_base = make_pim_kernel();
    DataDim *dim_base = new DataDim(KernelType::ADD, batch_size, output_dim, input_dim, true);
    BurstType* base_result = getResultPIM(KernelType::ADD, dim_base, kernel_base, nullptr);
    reportCycles(kernel_base, "add_1m [baseline]");

    shared_ptr<PIMKernel> kernel_pact = make_pim_kernel();
    DataDim *dim_pact = new DataDim(KernelType::ADD, batch_size, output_dim, input_dim, true);
    BurstType* pact_result = getResultPIMACT(KernelType::ADD, dim_pact, kernel_pact, nullptr);
    reportCycles(kernel_pact, "add_1m [PIM-ACT]");

    verifyResultsMatch(base_result, pact_result, dim_base->dimTobShape(output_dim), "add_1m");

    testStatsClear();
    delete[] base_result;
    delete[] pact_result;
    delete dim_base;
    delete dim_pact;
}

TEST_F(PIMKernelFixture, a3_mul_2m)
{
    // A3/Step4: MUL 2M cross-protocol correctness + cycle comparison.
    uint32_t batch_size = 1;
    uint32_t output_dim = 2 * 1024 * 1024;
    uint32_t input_dim  = output_dim;

    shared_ptr<PIMKernel> kernel_base = make_pim_kernel();
    DataDim *dim_base = new DataDim(KernelType::MUL, batch_size, output_dim, input_dim, true);
    BurstType* base_result = getResultPIM(KernelType::MUL, dim_base, kernel_base, nullptr);
    reportCycles(kernel_base, "mul_2m [baseline]");

    shared_ptr<PIMKernel> kernel_pact = make_pim_kernel();
    DataDim *dim_pact = new DataDim(KernelType::MUL, batch_size, output_dim, input_dim, true);
    BurstType* pact_result = getResultPIMACT(KernelType::MUL, dim_pact, kernel_pact, nullptr);
    reportCycles(kernel_pact, "mul_2m [PIM-ACT]");

    verifyResultsMatch(base_result, pact_result, dim_base->dimTobShape(output_dim), "mul_2m");

    testStatsClear();
    delete[] base_result;
    delete[] pact_result;
    delete dim_base;
    delete dim_pact;
}

TEST_F(PIMKernelFixture, a3_relu_4m)
{
    // A3/Step4: ReLU 4M cross-protocol correctness + cycle comparison.
    uint32_t output_dim = 4 * 1024 * 1024;
    uint32_t input_dim  = output_dim;

    shared_ptr<PIMKernel> kernel_base = make_pim_kernel();
    DataDim *dim_base = new DataDim(KernelType::RELU, 1, output_dim, input_dim, true);
    BurstType* base_result = getResultPIM(KernelType::RELU, dim_base, kernel_base, nullptr);
    reportCycles(kernel_base, "relu_4m [baseline]");

    shared_ptr<PIMKernel> kernel_pact = make_pim_kernel();
    DataDim *dim_pact = new DataDim(KernelType::RELU, 1, output_dim, input_dim, true);
    BurstType* pact_result = getResultPIMACT(KernelType::RELU, dim_pact, kernel_pact, nullptr);
    reportCycles(kernel_pact, "relu_4m [PIM-ACT]");

    verifyResultsMatch(base_result, pact_result, dim_base->dimTobShape(output_dim), "relu_4m");

    testStatsClear();
    delete[] base_result;
    delete[] pact_result;
    delete dim_base;
    delete dim_pact;
}