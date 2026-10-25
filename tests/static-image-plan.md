# Static image request and initializer plan

Run `node tests/static-image-plan.test.mjs <control.json>`. The control pins the actual module, the accepted flat59 ET_REL object, its bounded ELF reader and three independently expected constructor names. The required suite reads this real object, checks source section plus byte addends, exercises real request/evidence functions, and performs 512 repeat planning cycles. It launches no native tool or target bytes. Without the object/reader control, the command performs only the request and evidence subset; that subset does not satisfy the project test gate.

`planImageRequest` builds only a fixed loader assembly or a fixed final image request. The final request includes 59 distinct inherited individuals and one approved current loader in exact order, with separate ELF and map obligations. Callers must verify input pins, actual files and fresh output paths before launch. `imageExecSet` grants exact tools for those categories and no compiler.

`resolveInitializerEntries(bytes, parsed)` expects matching metadata from the caller's pinned bounded ET_REL parser. It reads each R_386_32 addend from bytes, resolves the real symbol table/index and section, and requires a unique constructor function. The current profile has plain `.init_array`; nonempty priority suffixes are refused pending an explicit policy. Offset-only rows and null symbol/addend assertions cannot satisfy this function. Derive expectations from individual inputs before inspecting a final image.

`inspectImageToolEvidence` checks captured run facts and per-category product obligations. The caller must read and hash actual files; supplied JSON alone is not an authenticity boundary. No pass proves boot, initialization, device activity, SMP or lifetime safety.
