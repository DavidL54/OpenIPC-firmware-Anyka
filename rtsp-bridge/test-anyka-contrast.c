#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ak_vpss.h"

static const enum vpss_effect_type image_types[] = {VPSS_EFFECT_BRIGHTNESS,VPSS_EFFECT_CONTRAST};
static struct vpss_contrast_attr current;
static int effect_value, fail_get, fail_attr_set, fail_effect_set, attr_calls;

int ak_vpss_get_contrast_attr(int dev,struct vpss_contrast_attr *out)
{
    assert(dev==0 && out);
    if(fail_get)return -1;
    *out=current;return 0;
}
int ak_vpss_set_contrast_attr(int dev,const struct vpss_contrast_attr *value)
{
    assert(dev==0 && value);attr_calls++;
    if(fail_attr_set)return -1;
    current=*value;return 0;
}
int ak_vpss_effect_set(int dev,enum vpss_effect_type type,const int value)
{
    assert(dev==0 && (type==VPSS_EFFECT_CONTRAST || type==VPSS_EFFECT_BRIGHTNESS));
    if(fail_effect_set || (type==VPSS_EFFECT_CONTRAST && current.cc_mode==VPSS_OP_TYPE_AUTO))return -1;
    effect_value=value;return 0;
}
#include "anyka-contrast.h"

int main(void)
{
    memset(&current,0,sizeof(current));
    current.cc_mode=VPSS_OP_TYPE_AUTO;
    current.linkage_contrast[0].dark_pixel_area=123;
    current.linkage_contrast[15].shift_max=55;
    struct vpss_contrast_attr day=current;
    assert(set_image_effect(1,0) && !contrast_profile_saved && !attr_calls);
    assert(set_image_effect(1,8) && contrast_profile_saved);
    assert(current.cc_mode==VPSS_OP_TYPE_MANUAL && current.manual_contrast.y_contrast==64 && effect_value==8);
    assert(!memcmp(&contrast_profile,&day,sizeof(day)));
    assert(set_image_effect(1,12) && effect_value==12 && attr_calls==1);
    assert(set_image_effect(1,0) && !contrast_profile_saved && !memcmp(&current,&day,sizeof(day)));
    assert(set_image_effect(1,25)); /* The profile remains adjustable after restoring Auto. */
    assert(set_image_effect(1,0) && !memcmp(&current,&day,sizeof(day)));
    fail_effect_set=1;
    assert(!set_image_effect(1,3) && !contrast_profile_saved && !memcmp(&current,&day,sizeof(day)));
    fail_effect_set=0;fail_attr_set=1;
    assert(!set_image_effect(1,3) && !contrast_profile_saved && !memcmp(&current,&day,sizeof(day)));
    fail_attr_set=0;fail_get=1;
    assert(!set_image_effect(1,3) && !contrast_profile_saved);
    fail_get=0;
    current.linkage_contrast[0].dark_pixel_area=222;
    struct vpss_contrast_attr night=current;
    assert(set_image_effect(1,-7) && !memcmp(&contrast_profile,&night,sizeof(night)));
    assert(set_image_effect(1,0) && !memcmp(&current,&night,sizeof(night)));
    assert(set_image_effect(0,5) && effect_value==5 && current.cc_mode==VPSS_OP_TYPE_AUTO);
    puts("AV130 contrast: manual adjustment, Auto restore, mode profile and failures OK");
}
