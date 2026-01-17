// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026, The Linux Foundation. All rights reserved.
 * Copyright (c) 2018, Pal Zoltan Illes (tbalden) - kcal rgb
 */
#include <linux/moduleparam.h>
#include <linux/export.h>
#include <drm/msm_drm_pp.h>
#include "sde_hw_color_proc_common_v4.h"
#include "sde_hw_color_proc_v4.h"

extern unsigned int kcal_red, kcal_green, kcal_blue;
extern unsigned int kcal_hue, kcal_sat, kcal_val, kcal_cont;

static int sde_write_3d_gamut(struct sde_hw_blk_reg_map *hw,
		struct drm_msm_3d_gamut *payload, u32 base,
		u32 *opcode, u32 pipe, u32 scale_tbl_a_len,
		u32 scale_tbl_b_len)
{
	u32 reg, tbl_len, tbl_off, scale_off, i, j;
	u32 scale_tbl_len, scale_tbl_off;
	u32 *scale_data;

	if (!payload || !opcode || !hw) {
		DRM_ERROR("invalid payload %pK opcode %pK hw %pK\n",
			payload, opcode, hw);
		return -EINVAL;
	}

	switch (payload->mode) {
	case GAMUT_3D_MODE_17:
		tbl_len = GAMUT_3D_MODE17_TBL_SZ;
		tbl_off = 0;
		if (pipe == DSPP) {
			scale_off = GAMUT_SCALEA_OFFSET_OFF;
			*opcode = gamut_mode_17;
		} else {
			*opcode = (*opcode & (BIT(5) - 1)) >> 2;
			if (*opcode == gamut_mode_17b)
				*opcode = gamut_mode_17;
			else
				*opcode = gamut_mode_17b;
			scale_off = (*opcode == gamut_mode_17) ?
				GAMUT_SCALEA_OFFSET_OFF :
				GAMUT_SCALEB_OFFSET_OFF;
		}
		break;
	case GAMUT_3D_MODE_13:
		*opcode = (*opcode & (BIT(4) - 1)) >> 2;
		if (*opcode == gamut_mode_13a)
			*opcode = gamut_mode_13b;
		else
			*opcode = gamut_mode_13a;
		tbl_len = GAMUT_3D_MODE13_TBL_SZ;
		tbl_off = (*opcode == gamut_mode_13a) ? 0 :
			GAMUT_MODE_13B_OFF;
		scale_off = (*opcode == gamut_mode_13a) ?
			GAMUT_SCALEA_OFFSET_OFF : GAMUT_SCALEB_OFFSET_OFF;
		*opcode <<= 2;
		break;
	case GAMUT_3D_MODE_5:
		*opcode = gamut_mode_5 << 2;
		tbl_len = GAMUT_3D_MODE5_TBL_SZ;
		tbl_off = GAMUT_MODE_5_OFF;
		scale_off = GAMUT_SCALEB_OFFSET_OFF;
		break;
	default:
		DRM_ERROR("invalid mode %d\n", payload->mode);
		return -EINVAL;
	}

	if (payload->flags & GAMUT_3D_MAP_EN)
		*opcode |= GAMUT_MAP_EN;
	*opcode |= GAMUT_EN;

	for (i = 0; i < GAMUT_3D_TBL_NUM; i++) {
		reg = GAMUT_TABLE0_SEL << i;
		reg |= ((tbl_off) & (BIT(11) - 1));
		SDE_REG_WRITE(hw, base + GAMUT_TABLE_SEL_OFF, reg);
		for (j = 0; j < tbl_len; j++) {
			SDE_REG_WRITE(hw, base + GAMUT_LOWER_COLOR_OFF,
					payload->col[i][j].c2_c1);
			SDE_REG_WRITE(hw, base + GAMUT_UPPER_COLOR_OFF,
					payload->col[i][j].c0);
		}
	}

	if ((*opcode & GAMUT_MAP_EN)) {
		if (scale_off == GAMUT_SCALEA_OFFSET_OFF)
			scale_tbl_len = scale_tbl_a_len;
		else
			scale_tbl_len = scale_tbl_b_len;
		for (i = 0; i < GAMUT_3D_SCALE_OFF_TBL_NUM; i++) {
			scale_tbl_off = base + scale_off +
					i * scale_tbl_len * sizeof(u32);
			scale_data = &payload->scale_off[i][0];
			for (j = 0; j < scale_tbl_len; j++)
				SDE_REG_WRITE(hw,
					scale_tbl_off + (j * sizeof(u32)),
					scale_data[j]);
		}
	}
	SDE_REG_WRITE(hw, base, *opcode);
	return 0;
}

void sde_setup_dspp_3d_gamutv4(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_3d_gamut *payload;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	u32 op_mode;

	if (!ctx || !cfg)
		return;

	op_mode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->gamut.base);
	if (!hw_cfg->payload) {
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->gamut.base, 0);
		return;
	}

	payload = hw_cfg->payload;
	sde_write_3d_gamut(&ctx->hw, payload, ctx->cap->sblk->gamut.base,
		&op_mode, DSPP, GAMUT_3D_SCALE_OFF_SZ, GAMUT_3D_SCALEB_OFF_SZ);

}

void sde_setup_dspp_3d_gamutv41(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_3d_gamut *payload;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	u32 op_mode;

	if (!ctx || !cfg)
		return;

	op_mode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->gamut.base);
	if (!hw_cfg->payload) {
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->gamut.base, 0);
		return;
	}

	payload = hw_cfg->payload;
	sde_write_3d_gamut(&ctx->hw, payload, ctx->cap->sblk->gamut.base,
		&op_mode, DSPP, GAMUT_3D_SCALE_OFF_SZ, GAMUT_3D_SCALE_OFF_SZ);
}

void sde_setup_dspp_igcv3(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_igc_lut *lut_cfg;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	int i = 0, j = 0;
	u32 *addr[IGC_TBL_NUM];
	u32 offset = 0;

	if (!ctx || !cfg || !hw_cfg->payload)
		return;

	lut_cfg = hw_cfg->payload;
	addr[0] = lut_cfg->c0;
	addr[1] = lut_cfg->c1;
	addr[2] = lut_cfg->c2;

	for (i = 0; i < IGC_TBL_NUM; i++) {
		offset = IGC_C0_OFF + (i * sizeof(u32));
		for (j = 0; j < IGC_TBL_LEN; j++) {
			addr[i][j] &= IGC_DATA_MASK;
			addr[i][j] |= IGC_DSPP_SEL_MASK(ctx->idx - 1);
			if (j == 0)
				addr[i][j] |= IGC_INDEX_UPDATE;
			SDE_REG_WRITE(&ctx->hw_top, offset, addr[i][j]);
		}
	}
	SDE_REG_WRITE(&ctx->hw, IGC_OPMODE_OFF, IGC_EN);
}

static void _sde_apply_kcal(struct sde_hw_dspp *ctx)
{
	u32 base, opcode;
	int kcal_min = 20;

	if (kcal_red < kcal_min) kcal_red = kcal_min;
	if (kcal_green < kcal_min) kcal_green = kcal_min;
	if (kcal_blue < kcal_min) kcal_blue = kcal_min;
	if (kcal_sat > 510) kcal_sat = 510;
	if (kcal_val > 510) kcal_val = 510;
	if (kcal_cont > 510) kcal_cont = 510;
	if (kcal_hue > 1535) kcal_hue = 1535;

	if (kcal_val > 450)
		return;

	base = ctx->cap->sblk->hsic.base;
	if (base) {
		opcode = SDE_REG_READ(&ctx->hw, base);
		SDE_REG_WRITE(&ctx->hw, base + PA_HUE_OFF, kcal_hue & PA_HUE_MASK);
		SDE_REG_WRITE(&ctx->hw, base + PA_SAT_OFF, kcal_sat & PA_SAT_MASK);
		SDE_REG_WRITE(&ctx->hw, base + PA_VAL_OFF, kcal_val & PA_VAL_MASK);
		SDE_REG_WRITE(&ctx->hw, base + PA_CONT_OFF, kcal_cont & PA_CONT_MASK);
		SDE_REG_WRITE(&ctx->hw, base, opcode | PA_HUE_EN | PA_SAT_EN | PA_VAL_EN | PA_CONT_EN | PA_EN);
	}
}

void sde_setup_dspp_pccv4(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_pcc *pcc_cfg;
	struct drm_msm_pcc_coeff *coeffs = NULL;
	u32 base;
	int i;

	if (!ctx || !cfg) return;

	_sde_apply_kcal(ctx);

	if (kcal_val > 450) {
		if (hw_cfg->payload) {
			pcc_cfg = hw_cfg->payload;
			for (i = 0; i < PCC_NUM_PLANES; i++) {
				base = ctx->cap->sblk->pcc.base + (i * sizeof(u32));
				switch (i) {
					case 0: coeffs = &pcc_cfg->r; break;
					case 1: coeffs = &pcc_cfg->g; break;
					case 2: coeffs = &pcc_cfg->b; break;
				}
				SDE_REG_WRITE(&ctx->hw, base + PCC_R_OFF, coeffs->r);
				SDE_REG_WRITE(&ctx->hw, base + PCC_G_OFF, coeffs->g);
				SDE_REG_WRITE(&ctx->hw, base + PCC_B_OFF, coeffs->b);
				SDE_REG_WRITE(&ctx->hw, base + PCC_C_OFF, coeffs->c);
				SDE_REG_WRITE(&ctx->hw, base + PCC_RG_OFF, coeffs->rg);
				SDE_REG_WRITE(&ctx->hw, base + PCC_RB_OFF, coeffs->rb);
				SDE_REG_WRITE(&ctx->hw, base + PCC_GB_OFF, coeffs->gb);
				SDE_REG_WRITE(&ctx->hw, base + PCC_RGB_OFF, coeffs->rgb);
			}
			SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->pcc.base, 1);
		} else {
			SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->pcc.base, 0);
		}
		return;
	}

	if (!hw_cfg->payload) {
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->pcc.base, 0);
		return;
	}

	pcc_cfg = hw_cfg->payload;
	for (i = 0; i < PCC_NUM_PLANES; i++) {
		base = ctx->cap->sblk->pcc.base + (i * sizeof(u32));
		switch (i) {
			case 0: coeffs = &pcc_cfg->r; break;
			case 1: coeffs = &pcc_cfg->g; break;
			case 2: coeffs = &pcc_cfg->b; break;
		}
		SDE_REG_WRITE(&ctx->hw, base + PCC_R_OFF, (coeffs->r * kcal_red) / 256);
		SDE_REG_WRITE(&ctx->hw, base + PCC_G_OFF, (coeffs->g * kcal_green) / 256);
		SDE_REG_WRITE(&ctx->hw, base + PCC_B_OFF, (coeffs->b * kcal_blue) / 256);
		SDE_REG_WRITE(&ctx->hw, base + PCC_C_OFF, coeffs->c);
		SDE_REG_WRITE(&ctx->hw, base + PCC_RG_OFF, coeffs->rg);
		SDE_REG_WRITE(&ctx->hw, base + PCC_RB_OFF, coeffs->rb);
		SDE_REG_WRITE(&ctx->hw, base + PCC_GB_OFF, coeffs->gb);
		SDE_REG_WRITE(&ctx->hw, base + PCC_RGB_OFF, coeffs->rgb);
	}
	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->pcc.base, 1);
}

void sde_setup_dspp_pccv41(struct sde_hw_dspp *ctx, void *cfg)
{
	sde_setup_dspp_pccv4(ctx, cfg);
}

void sde_setup_dspp_ltm_threshv1(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	u64 thresh;
	if (!ctx || !cfg || !hw_cfg->payload) return;
	thresh = *((u64 *)hw_cfg->payload);
	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->ltm.base + 0x60, (thresh & 0x3FF));
}

void sde_setup_dspp_ltm_hist_bufferv1(struct sde_hw_dspp *ctx, u64 addr)
{
	struct drm_msm_ltm_stats_data *hist;
	if (!ctx || !addr) return;
	hist = (struct drm_msm_ltm_stats_data *)addr;
	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->ltm.base + 0x70, (addr & 0xFFFFFF00));
	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->ltm.base + 0x74, ((u64)(&hist->stats_02[0]) & 0xFFFFFF00));
	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->ltm.base + 0x78, ((u64)(&hist->stats_03[0]) & 0xFFFFFF00));
}

void sde_setup_dspp_ltm_hist_ctrlv1(struct sde_hw_dspp *ctx, void *cfg, bool enable, u64 addr)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_ltm_phase_info phase;
	u32 op_mode, offset;

	if (!ctx) return;
	offset = ctx->cap->sblk->ltm.base + 0x4;
	op_mode = SDE_REG_READ(&ctx->hw, offset);

	if (!enable) {
		SDE_REG_WRITE(&ctx->hw, offset, (op_mode & BIT(1)) ? (op_mode & ~BIT(0)) : 0);
		return;
	}
	if (!addr || !cfg || ctx->idx >= DSPP_MAX) return;

	memset(&phase, 0, sizeof(phase));
	sde_ltm_get_phase_info(hw_cfg, &phase);
	op_mode = phase.portrait_en ? (op_mode | BIT(2)) : (op_mode & ~BIT(2));
	op_mode = phase.merge_en ? (op_mode | BIT(16)) : (op_mode & ~(BIT(16) | BIT(17)));

	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->ltm.base + 0x8, (phase.init_h[ctx->idx] & 0x7FFFFFF));
	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->ltm.base + 0xC, (phase.init_v & 0xFFFFFF));
	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->ltm.base + 0x10, (phase.inc_h & 0xFFFFFF));
	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->ltm.base + 0x14, (phase.inc_v & 0xFFFFFF));

	sde_setup_dspp_ltm_hist_bufferv1(ctx, addr);
	SDE_REG_WRITE(&ctx->hw, offset, (op_mode | BIT(0)) & 0x1FFFFFF);
}

void sde_ltm_read_intr_status(struct sde_hw_dspp *ctx, u32 *status)
{
	u32 clear;
	if (!ctx || !status) return;
	*status = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->ltm.base + 0x54);
	clear = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->ltm.base + 0x58);
	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->ltm.base + 0x58, clear | BIT(1) | BIT(2));
}
