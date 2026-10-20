/* AV130 rejects VPSS_EFFECT_CONTRAST while automatic Y gain is selected.
 * The SDK's VPSS sample switches cc_mode to manual before changing it.
 * Save the active ISP profile first so zero can restore its full contrast
 * attributes. The day/night controller invalidates this copy on a mode
 * switch, then captures the newly loaded profile before reapplying a user
 * override. This only changes contrast; exposure/white balance stay native.
 */
static struct vpss_contrast_attr contrast_profile;
static int contrast_profile_saved;

static int set_image_effect(int index, int value)
{
	if (index != 1)
		return !ak_vpss_effect_set(0, image_types[index], value);

	struct vpss_contrast_attr current;
	if (ak_vpss_get_contrast_attr(0, &current)) return 0;
	if (!value) {
		if (contrast_profile_saved) {
			if (ak_vpss_set_contrast_attr(0, &contrast_profile)) return 0;
			contrast_profile_saved = 0;
			return 1;
		}
		if (current.cc_mode == VPSS_OP_TYPE_AUTO) return 1;
		return !ak_vpss_effect_set(0, VPSS_EFFECT_CONTRAST, 0);
	}

	int was_saved = contrast_profile_saved;
	if (!was_saved) {
		contrast_profile = current;
		contrast_profile_saved = 1;
	}
	if (current.cc_mode == VPSS_OP_TYPE_AUTO) {
		struct vpss_contrast_attr manual = current;
		manual.cc_mode = VPSS_OP_TYPE_MANUAL;
		manual.manual_contrast.y_contrast = 64;
		manual.manual_contrast.y_shift = 0;
		if (ak_vpss_set_contrast_attr(0, &manual)) {
			if (!was_saved) contrast_profile_saved = 0;
			return 0;
		}
	}
	if (ak_vpss_effect_set(0, VPSS_EFFECT_CONTRAST, value)) {
		if (current.cc_mode == VPSS_OP_TYPE_AUTO)
			ak_vpss_set_contrast_attr(0, &current);
		if (!was_saved) contrast_profile_saved = 0;
		return 0;
	}
	return 1;
}
