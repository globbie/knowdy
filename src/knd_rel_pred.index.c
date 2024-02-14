static int index_inst_ref(struct kndClassInstEntry *topic_inst,
                          struct kndAttr *attr, struct kndAttrVar *var, struct kndTask *task)
{
    struct kndClass *spec;
    struct kndAttrHub *hub;
    struct kndClassInst *spec_inst;
    int err;

    switch (var->attr->rel_type) {
    case KND_REL_SUBCLASS:
        assert(var->class_entry != NULL);
        err = knd_class_acquire(var->class_entry, &spec, task);
        KND_TASK_ERR("failed to acquire class %.*s",
                     var->class_entry->name_size, var->class_entry->name);

        if (DEBUG_ATTR_VAR_IDX_LEVEL_3)
            knd_log(">> idx inst -> {class %.*s {inst %.*s}} -> {class %.*s}",
                    topic_inst->is_a->name_size, topic_inst->is_a->name,
                    topic_inst->name_size, topic_inst->name,
                    spec->name_size, spec->name);

        err = attr_hub_fetch(spec, attr, &hub, task);
        KND_TASK_ERR("failed to fetch attr hub");

        break;
    case KND_REL_CLASS_INST:
        assert(var->class_inst_entry != NULL);
        err = knd_class_inst_acquire(var->class_inst_entry, &spec_inst, task);
        KND_TASK_ERR("failed to acquire class inst %.*s",
                     var->class_inst_entry->name_size, var->class_inst_entry->name);

        if (DEBUG_ATTR_VAR_IDX_LEVEL_3)
            knd_log(">> idx inst --> inst {class %.*s {inst %.*s}}"
                    "\n  -- %.*s --> "
                    "\n  {class %.*s {inst %.*s}}",
                    topic_inst->is_a->name_size, topic_inst->is_a->name,
                    topic_inst->name_size, topic_inst->name,
                    attr->name_size, attr->name,
                    var->class_inst_entry->is_a->name_size,
                    var->class_inst_entry->is_a->name,
                    spec_inst->name_size, spec_inst->name);

        err = inst_attr_hub_fetch(spec_inst, attr, &hub, task);
        KND_TASK_ERR("failed to fetch inst attr hub");

        err = inst_attr_hub_add_inst(hub, topic_inst, task);
        KND_TASK_ERR("attr hub failed to add a topic inst");

        break;
    default:
        break;
    }

    /*
        err = attr_hub_add_classref(hub, topic, task);
        KND_TASK_ERR("attr hub failed to add a classref");
    */
    return knd_OK;
}
