// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 KaguyaRing
/* Offline compiler acceptance for repository shaders, not a device emulator.
 * Built inside pinned Mesa to use its actual NIR/NAK dependency graph. */
#include "compiler/spirv/nir_spirv.h"
#include "compiler/nir/nir.h"
#include "nouveau/compiler/nak.h"
#include "nouveau/headers/nv_device_info.h"
#include "util/ralloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
enum { MaxShaderBytes = 1048576, AdaShaderModel = 89, AdaWarpsPerMP = 48 };
static uint32_t *read_shader(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    uint32_t *words = malloc(MaxShaderBytes);
    if (!words) { fclose(file); return NULL; }
    *size = fread(words, 1, MaxShaderBytes, file);
    bool valid = !ferror(file) && *size >= 20 && *size % 4 == 0 &&
                 fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) valid = false;
    if (!valid || words[0] != UINT32_C(0x07230203)) { free(words); return NULL; }
    return words;
}
static bool compile_stage(struct nak_compiler *compiler, const char *path, mesa_shader_stage stage) {
    size_t size = 0;
    uint32_t *words = read_shader(path, &size);
    if (!words) { fprintf(stderr, "invalid bounded shader input: %s\n", path); return false; }
    struct spirv_to_nir_options options = {
        .environment = NIR_SPIRV_VULKAN,
        .ubo_addr_format = nir_address_format_64bit_global,
        .ssbo_addr_format = nir_address_format_64bit_global,
        .phys_ssbo_addr_format = nir_address_format_64bit_global,
        .shared_addr_format = nir_address_format_32bit_offset,
    };
    nir_shader *nir = spirv_to_nir(words, size / 4, NULL, stage, "main", &options,
                                  nak_nir_options(compiler));
    free(words);
    if (!nir) return false;
    NIR_PASS(_, nir, nir_lower_variable_initializers, nir_var_function_temp);
    NIR_PASS(_, nir, nir_lower_returns);
    NIR_PASS(_, nir, nir_inline_functions);
    NIR_PASS(_, nir, nir_opt_copy_prop);
    NIR_PASS(_, nir, nir_opt_constant_folding);
    NIR_PASS(_, nir, nir_opt_deref);
    nir_remove_non_cmat_call_entrypoints(nir);
    NIR_PASS(_, nir, nir_lower_variable_initializers, (nir_variable_mode)~0u);
    NIR_PASS(_, nir, nir_split_var_copies);
    NIR_PASS(_, nir, nir_split_per_member_structs);
    NIR_PASS(_, nir, nir_remove_dead_variables,
             nir_var_shader_in | nir_var_shader_out | nir_var_system_value, NULL);
    nir_gather_clip_cull_distance_sizes_from_vars(nir);
    NIR_PASS(_, nir, nir_merge_clip_cull_distance_vars);
    nak_preprocess_nir(nir, compiler);
    const struct nak_fs_key fs_key = {0};
    struct nak_shader_bin *bin = nak_compile_shader(nir, true, compiler, 0,
        stage == MESA_SHADER_FRAGMENT ? &fs_key : NULL, false);
    bool valid = bin && bin->code && bin->code_size > 0 && bin->code_size % 16 == 0 &&
                 bin->asm_str && bin->asm_str[0] && bin->info.stage == stage;
    if (valid) {
        printf("PASS offline SM89 %s: %u native code bytes\n%s\n",
               stage == MESA_SHADER_VERTEX ? "vertex" : "fragment", bin->code_size, bin->asm_str);
    } else {
        fprintf(stderr, "NAK returned no valid native stage binary: %s\n", path);
    }
    if (bin) nak_shader_bin_destroy(bin);
    ralloc_free(nir);
    return valid;
}
int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <validated vertex SPIR-V> <validated fragment SPIR-V>\n", argv[0]); return 2; }
    /* Explicit compilation target, not queried or advertised GPU identity. */
    const struct nv_device_info target = { .sm = AdaShaderModel, .max_warps_per_mp = AdaWarpsPerMP };
    glsl_type_singleton_init_or_ref();
    struct nak_compiler *compiler = nak_compiler_create(&target);
    if (!compiler) { glsl_type_singleton_decref(); return 1; }
    bool valid = compile_stage(compiler, argv[1], MESA_SHADER_VERTEX);
    if (valid) valid = compile_stage(compiler, argv[2], MESA_SHADER_FRAGMENT);
    nak_compiler_destroy(compiler);
    glsl_type_singleton_decref();
    if (valid) puts("Both repository graphics stages compiled; no GPU submission or rendering claimed");
    return valid ? 0 : 1;
}
