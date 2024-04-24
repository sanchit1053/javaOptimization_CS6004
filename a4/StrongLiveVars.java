import java.util.Set;

import soot.Local;
import soot.Unit;

public interface StrongLiveVars {
    Set<Local> getLiveLocalsAfter(Unit u);
    Set<Local> getLiveLocalsBefore(Unit u);
}
