/*******************************************************************************
* Copyright 2020-2024 Intel Corporation
* Copyright 2020-2022 Codeplay Software Limited
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "gpu/nvidia/cudnn_inner_product.hpp"
#include "gpu/nvidia/cudnn_conv_inner_product.hpp"
#include "gpu/nvidia/cudnn_gemm_inner_product.hpp"
#include "gpu/nvidia/stream.hpp"
#include "gpu/nvidia/sycl_cuda_scoped_context.hpp"
#include "xpu/sycl/buffer_memory_storage.hpp"
#include "xpu/sycl/memory_storage_helper.hpp"

namespace dnnl {
namespace impl {
namespace gpu {
namespace nvidia {

status_t cudnn_inner_product_fwd_t::execute(const exec_ctx_t &ctx) const {
    if (pd()->has_zero_dim_memory()) return status::success;

    nvidia::stream_t *cuda_stream
            = utils::downcast<nvidia::stream_t *>(ctx.stream());

    return cuda_stream->interop_task([&](::sycl::handler &cgh) {
        auto arg_src = CTX_IN_SYCL_MEMORY(DNNL_ARG_SRC);
        auto arg_wei = CTX_IN_SYCL_MEMORY(DNNL_ARG_WEIGHTS);
        auto arg_bias = CTX_IN_SYCL_MEMORY(DNNL_ARG_BIAS);
        auto arg_dst = CTX_OUT_SYCL_MEMORY(DNNL_ARG_DST);
        auto arg_src_scale
                = CTX_IN_SYCL_MEMORY(DNNL_ARG_ATTR_SCALES | DNNL_ARG_SRC);
        auto arg_wei_scale
                = CTX_IN_SYCL_MEMORY(DNNL_ARG_ATTR_SCALES | DNNL_ARG_WEIGHTS);
        auto arg_dst_scale
                = CTX_IN_SYCL_MEMORY(DNNL_ARG_ATTR_SCALES | DNNL_ARG_DST);
        auto arg_ip_scratch = CTX_SCRATCH_SYCL_MEMORY(
                memory_tracking::names::key_iprod_int_dat_in_acc_dt);
        auto arg_spacial_scratch
                = CTX_SCRATCH_SYCL_MEMORY(memory_tracking::names::key_none);
        auto arg_f32_bias_scratch = CTX_SCRATCH_SYCL_MEMORY(
                memory_tracking::names::key_iprod_bias_bf16_convert_wsp);
        compat::host_task(cgh, [=, this](const compat::interop_handle &ih) {
            auto &sycl_engine = *utils::downcast<nvidia::engine_t *>(
                    cuda_stream->engine());
            auto sc = cuda_sycl_scoped_context_handler_t(sycl_engine);
            // SYCL out-of-order queue encapsulates multiple CUstream objects.
            // Every query of the CUstream object can return any of those
            // therefore we need to make sure that we activate both cuDNN and
            // cuBLAS handles for the same CUstream object.
            auto native_stream = cuda_stream->get_underlying_stream();
            auto cublas_handle = cuda_stream->get_cublas_handle(native_stream);
            auto cudnn_handle = cuda_stream->get_cudnn_handle(native_stream);

            std::vector<void *> args;

            args.push_back(arg_src.get_native_pointer(ih));
            args.push_back(arg_wei.get_native_pointer(ih));
            args.push_back(arg_bias.get_native_pointer(ih));
            args.push_back(arg_dst.get_native_pointer(ih));
            args.push_back(arg_ip_scratch.get_native_pointer(ih));
            args.push_back(arg_spacial_scratch.get_native_pointer(ih));
            args.push_back(arg_src_scale.get_native_pointer(ih));
            args.push_back(arg_wei_scale.get_native_pointer(ih));
            args.push_back(arg_dst_scale.get_native_pointer(ih));
            args.push_back(arg_f32_bias_scratch.get_native_pointer(ih));

            pd()->inner_product_impl_->execute(
                    cudnn_handle, cublas_handle, args);
        });
    });
}

status_t cudnn_inner_product_bwd_data_t::execute(const exec_ctx_t &ctx) const {
    if (pd()->has_zero_dim_memory()) return status::success;
    nvidia::stream_t *cuda_stream
            = utils::downcast<nvidia::stream_t *>(ctx.stream());

    return cuda_stream->interop_task([&](::sycl::handler &cgh) {
        auto arg_diff_dst = CTX_IN_SYCL_MEMORY(DNNL_ARG_DIFF_DST);
        auto arg_wei = CTX_IN_SYCL_MEMORY(DNNL_ARG_WEIGHTS);
        auto arg_diff_src = CTX_OUT_SYCL_MEMORY(DNNL_ARG_DIFF_SRC);
        auto arg_ip_scratch = CTX_SCRATCH_SYCL_MEMORY(
                memory_tracking::names::key_iprod_int_dat_in_acc_dt);
        auto arg_spacial_scratch
                = CTX_SCRATCH_SYCL_MEMORY(memory_tracking::names::key_none);

        compat::host_task(cgh, [=, this](const compat::interop_handle &ih) {
            auto &sycl_engine = *utils::downcast<nvidia::engine_t *>(
                    cuda_stream->engine());
            auto sc = cuda_sycl_scoped_context_handler_t(sycl_engine);
            // SYCL out-of-order queue encapsulates multiple CUstream objects.
            // Every query of the CUstream object can return any of those
            // therefore we need to make sure that we activate both cuDNN and
            // cuBLAS handles for the same CUstream object.
            auto native_stream = cuda_stream->get_underlying_stream();
            auto cublas_handle = cuda_stream->get_cublas_handle(native_stream);
            auto cudnn_handle = cuda_stream->get_cudnn_handle(native_stream);

            std::vector<void *> args;

            args.push_back(arg_diff_src.get_native_pointer(ih));
            args.push_back(arg_wei.get_native_pointer(ih));
            args.push_back(arg_diff_dst.get_native_pointer(ih));
            args.push_back(arg_ip_scratch.get_native_pointer(ih));
            args.push_back(arg_spacial_scratch.get_native_pointer(ih));

            pd()->inner_product_impl_->execute(
                    cudnn_handle, cublas_handle, args);
        });
    });
}

status_t cudnn_inner_product_bwd_weights_t::execute(
        const exec_ctx_t &ctx) const {

    nvidia::stream_t *cuda_stream
            = utils::downcast<nvidia::stream_t *>(ctx.stream());

    if (pd()->has_zero_dim_memory()) {
        auto wei_sz = memory_desc_wrapper(pd()->diff_weights_md(0)).size();
        size_t bias_sz = (pd()->with_bias()
                        ? memory_desc_wrapper(pd()->diff_weights_md(1)).size()
                        : 0);

        if (wei_sz != 0) {
            auto status
                    = cuda_stream->fill(CTX_OUT_STORAGE(DNNL_ARG_DIFF_WEIGHTS),
                            0, wei_sz, cuda_stream->ctx().get_deps(),
                            cuda_stream->ctx().get_deps());
            if (status != status::success) return status;
        }
        if (bias_sz != 0) {
            auto status = cuda_stream->fill(CTX_OUT_STORAGE(DNNL_ARG_DIFF_BIAS),
                    0, bias_sz, cuda_stream->ctx().get_deps(),
                    cuda_stream->ctx().get_deps());
            if (status != status::success) return status;
        }
        return status::success;
    }

    return cuda_stream->interop_task([&](::sycl::handler &cgh) {
        auto arg_src = CTX_IN_SYCL_MEMORY(DNNL_ARG_SRC);
        auto arg_diff_dst = CTX_IN_SYCL_MEMORY(DNNL_ARG_DIFF_DST);
        auto arg_diff_wei = CTX_OUT_SYCL_MEMORY(DNNL_ARG_DIFF_WEIGHTS);
        auto arg_bias = CTX_OUT_SYCL_MEMORY(DNNL_ARG_DIFF_BIAS);
        auto arg_ip_scratch = CTX_SCRATCH_SYCL_MEMORY(
                memory_tracking::names::key_iprod_int_dat_in_acc_dt);
        auto arg_spacial_scratch
                = CTX_SCRATCH_SYCL_MEMORY(memory_tracking::names::key_none);

        compat::host_task(cgh, [=, this](const compat::interop_handle &ih) {
            auto &sycl_engine = *utils::downcast<nvidia::engine_t *>(
                    cuda_stream->engine());
            auto sc = cuda_sycl_scoped_context_handler_t(sycl_engine);
            // SYCL out-of-order queue encapsulates multiple CUstream objects.
            // Every query of the CUstream object can return any of those
            // therefore we need to make sure that we activate both cuDNN and
            // cuBLAS handles for the same CUstream object.
            auto native_stream = cuda_stream->get_underlying_stream();
            auto cublas_handle = cuda_stream->get_cublas_handle(native_stream);
            auto cudnn_handle = cuda_stream->get_cudnn_handle(native_stream);
            std::vector<void *> args;

            args.push_back(arg_src.get_native_pointer(ih));
            args.push_back(arg_diff_dst.get_native_pointer(ih));
            args.push_back(arg_diff_wei.get_native_pointer(ih));
            args.push_back(arg_bias.get_native_pointer(ih));
            args.push_back(arg_ip_scratch.get_native_pointer(ih));
            args.push_back(arg_spacial_scratch.get_native_pointer(ih));

            pd()->inner_product_impl_->execute(
                    cudnn_handle, cublas_handle, args);
        });
    });
}

} // namespace nvidia
} // namespace gpu
} // namespace impl
} // namespace dnnl
