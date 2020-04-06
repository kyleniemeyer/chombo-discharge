import os
import sys

# Write an options file. This should be a separate routine
def write_template(args):
    app_dir = args.plasmac_home + "/" + args.base_dir + "/" + args.app_name
    options_filename = app_dir + "/template.inputs"
    optf = open(options_filename, 'w')
    
    # Write plasma kinetics options
    options_files = [args.plasmac_home + "/src/amr_mesh/amr_mesh.options", \
                     args.plasmac_home + "/src/driver/driver.options", \
                     args.plasmac_home + "/src/cdr_solver/" + args.cdr_solver + ".options",\
                     args.plasmac_home + "/src/geometry/geo_coarsener.options", \
                     args.plasmac_home + "/geometries/" + args.geometry + "/" + args.geometry + ".options", \
                     args.plasmac_home + "/physics/advection_diffusion/advection_diffusion_stepper.options"]

    for opt in options_files:
        if os.path.exists(opt):
            f = open(opt, 'r')
            lines = f.readlines()
            optf.writelines(lines)
            optf.write('\n\n')
            f.close()
        else:
            print 'Could not find options file (this _may_ be normal behavior) ' + opt
    optf.close()