// Browser entry for the exhibition preview page: the web's own pattern runtime
// and annotation parsers, bundled by preview.ts into one inline script. The
// page renders patterns exactly as Pattern Lab does.
import { PatternRuntime } from "@/lib/pattern/harness";
import { parseKnobsAnnotation } from "@/lib/pattern/knobs";
import { parseMatrixAnnotation } from "@/lib/pattern/matrix";
import { parseRampAnnotation } from "@/lib/pattern/ramp";
import { hexToRgb } from "@/lib/pattern/color";

(globalThis as unknown as { PF: unknown }).PF = {
  PatternRuntime,
  parseKnobsAnnotation,
  parseMatrixAnnotation,
  parseRampAnnotation,
  hexToRgb,
};
