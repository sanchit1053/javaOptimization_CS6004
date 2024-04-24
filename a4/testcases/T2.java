//pure method classification
//inter-procedural const. prop. on pure methods.
public class T2 {
    static int global;
    static double foo(double a, double b){
        double c = a;
        c = c+1;
        if(c==b){
            c = c+1;
        }
        return a+b;
    }
    public static void main(String[] args) {
        double a = 1;
        double b = 2;
        double c = 3;
        a = a+1;
        b = b*2*a;
        c = c+1*c-2;
        b = a+c;
        c = T2.foo(a,c);
        c = c+b;
        c = a*c;
        System.out.println(c);
    }
}
