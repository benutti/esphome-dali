from esphome.components import output
from esphome.const import CONF_ID

import esphome.codegen as cg
import esphome.config_validation as cv

from . import dali_ns, CONF_DALI_BUS, DaliBusComponent

DaliOutput = dali_ns.class_('DaliOutput', output.FloatOutput, cg.Component)

CONFIG_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(DaliOutput),
    cv.GenerateID(CONF_DALI_BUS): cv.use_id(DaliBusComponent),
}).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    parent = yield cg.get_variable(config[CONF_DALI_BUS])
    var = cg.new_Pvariable(config[CONF_ID], parent)
    yield cg.register_component(var, config)
    yield output.register_output(var, config)

