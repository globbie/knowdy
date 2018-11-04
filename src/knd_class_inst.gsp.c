#include "knd_class_inst.h"

#include "knd_elem.h"
#include "knd_utils.h"

#include "knd_attr.h"  // FIXME(k15tfu): ?? remove this

static int export_inner_GSP(struct kndClassInst *self,
                            struct glbOutput *out)
{
    struct kndElem *elem;
    int err;

    /* anonymous obj */
    //err = out->writec(out, '{');   RET_ERR();

    elem = self->elems;
    while (elem) {
        err = knd_elem_export(elem, KND_FORMAT_GSP, out);
        if (err) {
            knd_log("-- inst elem GSP export failed: %.*s",
                    elem->attr->name_size, elem->attr->name);
            return err;
        }
        elem = elem->next;
    }
    //err = out->writec(out, '}');   RET_ERR();

    return knd_OK;
}

int knd_class_inst_export_GSP(struct kndClassInst *self, struct glbOutput *out)
{
    struct kndElem *elem;
    int err;

    if (self->type == KND_OBJ_INNER) {
        err = export_inner_GSP(self, out);
        if (err) {
            knd_log("-- inner obj GSP export failed");
            return err;
        }
        return knd_OK;
    }

    /* elems */
    for (elem = self->elems; elem; elem = elem->next) {
        err = knd_elem_export(elem, KND_FORMAT_GSP, out);
        if (err) {
            knd_log("-- export of \"%s\" elem failed: %d :(",
                    elem->attr->name, err);
            return err;
        }
    }

    return knd_OK;
}
