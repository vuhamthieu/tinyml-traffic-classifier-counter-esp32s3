import * as React from "react";

function cx(...classes: Array<string | undefined | false | null>) {
  return classes.filter(Boolean).join(" ");
}

type AlertVariant = "default" | "destructive";

export const Alert = React.forwardRef<
  HTMLDivElement,
  React.HTMLAttributes<HTMLDivElement> & { variant?: AlertVariant }
>(function Alert({ className, variant = "default", ...props }, ref) {
  return (
    <div
      ref={ref}
      role="alert"
      className={cx(
        "rounded-lg border p-4",
        variant === "destructive"
          ? "border-red-500/40 bg-red-500/10 text-foreground"
          : "bg-background text-foreground",
        className,
      )}
      {...props}
    />
  );
});

export const AlertTitle = React.forwardRef<
  HTMLParagraphElement,
  React.HTMLAttributes<HTMLParagraphElement>
>(function AlertTitle({ className, ...props }, ref) {
  return (
    <p ref={ref} className={cx("font-medium leading-none", className)} {...props} />
  );
});

export const AlertDescription = React.forwardRef<
  HTMLParagraphElement,
  React.HTMLAttributes<HTMLParagraphElement>
>(function AlertDescription({ className, ...props }, ref) {
  return (
    <p
      ref={ref}
      className={cx("mt-2 text-sm text-foreground/80", className)}
      {...props}
    />
  );
});
