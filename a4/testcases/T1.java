//pure method classification
//inter-procedural const. prop. on pure methods.
public class T1 {
    static int global;
    static int foo(int a, int b){
        int c = a;
        c = c+1;
        if(c==b){
            c = c+1;
        }
        return a+b;
    }
    public static void main(String[] args) {
        int a = 1;
        int b = 2;
        int c = 3;
        a = a+1;
        b = b*2*a;
        c = c+1*c-2;
        b = a+c;
        c = T1.foo(a,c);
        c = c+b;
        c = a*c;
        System.out.println(c);
    }
}
