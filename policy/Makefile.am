polkit_policydir = $(datadir)/polkit-1/actions

dist_polkit_policy_in_files = org.freedesktop.network-manager-settings.system.policy.in
dist_polkit_policy_DATA = $(dist_polkit_policy_in_files:.policy.in=.policy)

@INTLTOOL_POLICY_RULE@

# polkit >= 0.92 doesn't have an updated polkit-policy-file-validate
#check:
#	@for f in $(dist_polkit_policy_DATA); do \
#            echo -n "Validate PolicyKit policy in $$f : "; \
#            $(POLKIT_POLICY_FILE_VALIDATE) $(srcdir)/$$f ; \
#	    ret=$$?; \
#            if  [ "$$ret" = "0" ]; \
#	      then \
#                echo ok; \
#            else \
#                echo failed; \
#		exit 1; \
#            fi; \
#        done;

clean-local :
	rm -f *~

EXTRA_DIST = $(dist_polkit_policy_in_files)
