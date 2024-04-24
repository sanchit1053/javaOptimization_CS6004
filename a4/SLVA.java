import java.util.HashSet;
import java.util.Set;

import soot.Local;
import soot.Unit;
import soot.Value;
import soot.ValueBox;
import soot.toolkits.graph.BriefUnitGraph;
import soot.toolkits.scalar.ArraySparseSet;
import soot.toolkits.scalar.BackwardFlowAnalysis;
import soot.toolkits.scalar.FlowSet;

public class SLVA extends BackwardFlowAnalysis<Unit, FlowSet<Local>> implements StrongLiveVars {
    SLVA(BriefUnitGraph g) {
        super(g);
        this.doAnalysis();
    }

    @Override
    public Set<Local> getLiveLocalsAfter(Unit unit) {
        return new HashSet<>(getFlowAfter(unit).toList());
    }

    @Override
    public Set<Local> getLiveLocalsBefore(Unit unit) {
        return new HashSet<>(getFlowBefore(unit).toList());
    }

    @Override
    protected void flowThrough(FlowSet<Local> in, Unit unit, FlowSet<Local> out) {
        //kill
        in.copy(out);
        boolean liveDef = false;
        for (ValueBox box : unit.getDefBoxes()) {
            Value v = box.getValue();
            if (v instanceof Local) {
                Local local = (Local) v;
                out.remove(local);
                liveDef = in.contains(local);
            }
        }
        liveDef = liveDef || (unit.getDefBoxes().isEmpty());
        //gen
        for (ValueBox box : unit.getUseBoxes()) {
            Value v = box.getValue();
            if (v instanceof Local) {
                Local local = (Local) v;
                if (liveDef) {// strong check
                    out.add(local);
                }
            }
        }

    }

    @Override
    protected void copy(FlowSet<Local> src, FlowSet<Local> dst) {
        src.copy(dst);
    }

    @Override
    protected void merge(FlowSet<Local> in1, FlowSet<Local> in2, FlowSet<Local> out) {
        in1.union(in2, out);

    }

    @Override
    protected FlowSet<Local> newInitialFlow() {
        return new ArraySparseSet<Local>();
    }
}
