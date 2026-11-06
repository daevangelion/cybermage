#include "ui.h"
#include "audio.h"

#include <string.h>

static int dac_clip(int value)
{
    return value < 0 ? 0 : value > 63 ? 63 : value;
}

/* CM_HI.EXE SetGammaBright5253C then SetGammaContrast52598. */
int ui_apply_settings(cm_ui *ui, uint8_t palette[768], const uint8_t base[768])
{
    unsigned i, c;
    int bright=ui->options[6], contrast=ui->options[7];
    if (bright < -32 || bright > 32 || contrast < -32 || contrast > 32)
        return cm_fail("options",6,"gamma outside original slider range");
    for (i=3;i<=5;++i)
        if (ui->options[i]<0 || ui->options[i]>100)
            return cm_fail("options",i,"volume outside slider range");
    for (i=0;i<768;i+=3) {
        int rgb[3], mean;
        for (c=0;c<3;++c) rgb[c]=dac_clip((base[i+c]>>2)+bright);
        mean=(rgb[0]+rgb[1]+rgb[2])/3;
        for (c=0;c<3;++c)
            palette[i+c]=(uint8_t)(dac_clip(rgb[c]+(mean<32?-contrast:contrast))*4);
    }
    if (!ui->volumes_applied ||
        memcmp(ui->applied_volumes,ui->options+3,sizeof ui->applied_volumes)) {
        snd_volumes(ui->options[3]*AUDIO_VOLUME_MAX/100,
                    ui->options[4]*AUDIO_VOLUME_MAX/100,
                    (unsigned)ui->options[5]*AUDIO_MIDI_VOLUME_MAX/100);
        memcpy(ui->applied_volumes,ui->options+3,sizeof ui->applied_volumes);
        ui->volumes_applied=1;
    }
    return 0;
}
