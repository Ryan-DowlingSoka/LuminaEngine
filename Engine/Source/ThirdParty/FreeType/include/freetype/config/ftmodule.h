/*
 * Lumina-trimmed module registration. Legacy formats (Type1, CID, PFR,
 * Type42, WinFNT, PCF, BDF) and the SDF/SVG renderers were stripped with
 * their source dirs. Re-adding a module needs both src/<dir>/ and an entry here.
 */

FT_USE_MODULE( FT_Module_Class,    autofit_module_class      )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class           )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class          )
FT_USE_MODULE( FT_Module_Class,    psaux_module_class        )
FT_USE_MODULE( FT_Module_Class,    psnames_module_class      )
FT_USE_MODULE( FT_Module_Class,    pshinter_module_class     )
FT_USE_MODULE( FT_Module_Class,    sfnt_module_class         )
FT_USE_MODULE( FT_Renderer_Class,  ft_smooth_renderer_class  )
FT_USE_MODULE( FT_Renderer_Class,  ft_raster1_renderer_class )

/* EOF */
